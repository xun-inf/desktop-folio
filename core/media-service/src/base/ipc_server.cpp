#include "ipc_server.h"
#include "base/ipc_server_p.h"
#include "service_registry.h"

#include "base/logger.h"
#include "event_loop.h"

#include <cstddef>
#include <exception>
#include <utility>

// 消息类型：core/schemas/service.fbs 的 MessageType 枚举（None=0 / InvokeReq=1 / InvokeRsp=2 / Event=3）。

// IpcServer implementation
IpcServer::IpcServer(DisconnectCallback callback) : d_ptr(new IpcServerPrivate(this, std::move(callback)))
{
}

IpcServer::~IpcServer()
{
}

void IpcServer::setRegistry(ServiceRegistry* registry)
{
    d_ptr->setRegistry(registry);
}

void IpcServer::setEventLoop(EventLoop* eventLoop)
{
    d_ptr->setEventLoop(eventLoop);
}

bool IpcServer::connected() const
{
    return d_ptr->connected();
}

bool IpcServer::start(const std::string& pipeName)
{
    return d_ptr->start(pipeName);
}

void IpcServer::stop()
{
    d_ptr->stop();
}

bool IpcServer::send(const std::vector<uint8_t>& msg)
{
    return d_ptr->send(msg);
}

bool IpcServer::reply(uint64_t requestId, bool success, const std::string& message,
                      const std::vector<uint8_t>& resultBytes)
{
    if (requestId == 0)
    {
        return true;
    }
    flatbuffers::FlatBufferBuilder builder;
    const auto serviceId = builder.CreateString("");
    const auto text = builder.CreateString(message);
    const auto result = resultBytes.empty() ? 0 : builder.CreateVector(resultBytes);
    const auto frame = services::CreateInvokeRsp(builder, services::MessageType_InvokeRsp, serviceId, requestId,
                                                 session(), success, text, result);
    builder.Finish(frame);
    return send({builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()});
}

bool IpcServer::emitEvent(const std::string& serviceId, const std::string& event,
                          const std::vector<uint8_t>& payloadBytes)
{
    flatbuffers::FlatBufferBuilder builder;
    const auto serviceIdOffset = builder.CreateString(serviceId);
    const auto eventOffset = builder.CreateString(event);
    const auto payload = payloadBytes.empty() ? 0 : builder.CreateVector(payloadBytes);
    const auto frame =
        services::CreateServiceEvent(builder, services::MessageType_Event, serviceIdOffset, eventOffset, payload);
    builder.Finish(frame);
    return send({builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()});
}

namespace
{

constexpr std::size_t kMaxMessageSize = 8 * 1024 * 1024;

void logWarning(const char* message) noexcept
{
    try
    {
        mediaservice::logger().logWarn("ipc", message);
    }
    catch (...)
    {
        // 日志失败也不能让异常越过 IPC 线程边界。
    }
}

} // namespace

IpcServerPrivate::IpcServerPrivate(IpcServer* owner, DisconnectCallback callback)
    : m_owner(owner), m_disconnectCallback(std::move(callback))
{
}

void IpcServerPrivate::setRegistry(ServiceRegistry* registry)
{
    m_registry = registry;
}

void IpcServerPrivate::setEventLoop(EventLoop* eventLoop)
{
    m_eventLoop = eventLoop;
}

bool IpcServerPrivate::connected() const
{
    return m_connected.load();
}

bool IpcServerPrivate::reply(uint64_t requestId, bool success, const std::string& message,
                             const std::vector<uint8_t>& resultBytes)
{
    if (requestId == 0)
    {
        return true;
    }
    flatbuffers::FlatBufferBuilder builder;
    const auto serviceId = builder.CreateString("");
    const auto text = builder.CreateString(message);
    const auto result = resultBytes.empty() ? 0 : builder.CreateVector(resultBytes);
    const auto frame = services::CreateInvokeRsp(builder, services::MessageType_InvokeRsp, serviceId, requestId,
                                                 m_owner->session(), success, text, result);
    builder.Finish(frame);
    return send({builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()});
}

bool IpcServerPrivate::dispatchInvoke(const std::vector<uint8_t>& frame) noexcept
{
    try
    {
        if (frame.empty() || frame.size() > kMaxMessageSize)
        {
            logWarning("invalid invoke frame size");
            return false;
        }
        flatbuffers::Verifier verifier(frame.data(), frame.size());
        if (!services::VerifyInvokeReqBuffer(verifier))
        {
            logWarning("invalid InvokeReq");
            return false;
        }
        const services::InvokeReq* invoke = services::GetInvokeReq(frame.data());
        const std::string serviceId = invoke->service_id() ? invoke->service_id()->str() : std::string();
        const std::string method = invoke->method() ? invoke->method()->str() : std::string();
        const std::uint64_t requestId = invoke->request_id();
        if (serviceId.empty() || method.empty() || requestId == 0)
        {
            logWarning("malformed InvokeReq fields");
            return false;
        }

        // 会话校验：hello 必须是首次握手（session == 0）；其余必须匹配当前会话。
        const bool isHello = serviceId == "SessionService" && method == "hello";
        if (isHello)
        {
            if (invoke->session_id() != 0)
            {
                return false;
            }
        }
        else if (invoke->session_id() == 0 || invoke->session_id() != m_owner->session())
        {
            logWarning("session mismatch");
            return false;
        }

        // postTask 后 frame 可能被销毁，args 必须拷贝进任务。
        std::vector<std::uint8_t> argsCopy;
        if (invoke->args() != nullptr && invoke->args()->size() != 0)
        {
            argsCopy.assign(invoke->args()->data(), invoke->args()->data() + invoke->args()->size());
        }
        const std::string frameServiceId = serviceId;
        const std::string frameMethod = method;

        auto execute = [this, frameServiceId, frameMethod, argsCopy, requestId]()
        {
            if (m_registry == nullptr)
            {
                return;
            }
            Service::ReplyWriter writer;
            writer.write = [this, requestId](bool success, const std::string& message,
                                             const std::vector<std::uint8_t>& resultBytes)
            { reply(requestId, success, message, resultBytes); };
            const std::uint8_t* args = argsCopy.empty() ? nullptr : argsCopy.data();
            m_registry->dispatch(frameServiceId.c_str(), frameMethod.c_str(), args, argsCopy.size(), writer);
        };

        // SessionService（握手/退出）在 IPC 线程同步处理，不依赖 UI 线程。
        if (serviceId == "SessionService")
        {
            execute();
            return true;
        }
        if (m_eventLoop == nullptr || !m_eventLoop->postTask(execute))
        {
            logWarning("failed to post invoke task");
            return false;
        }
        return true;
    }
    catch (const std::exception& error)
    {
        logWarning(error.what());
        return false;
    }
    catch (...)
    {
        logWarning("invoke dispatch threw an unknown exception");
        return false;
    }
}
