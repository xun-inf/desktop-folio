#include "runtime/transport.h"

#include "runtime/napi_util.h"

#include <cstring>

namespace node_media_service
{
namespace
{

constexpr std::size_t kPrefixSize = 4;
constexpr std::uint32_t kMaxFrameSize = 8 * 1024 * 1024;
constexpr std::size_t kMinFlatBufferSize = 8;

std::string eventKey(const char* serviceId, const char* event)
{
    std::string key(serviceId);
    key += '\n';
    key += event;
    return key;
}

// TSFN 的 JS 线程回调：context = Transport*，data = DispatchItem*。
void transportCallJs(napi_env env, napi_value, void* context, void* data)
{
    Transport* self = static_cast<Transport*>(context);
    self->callJs(env, static_cast<DispatchItem*>(data));
}

} // namespace

Transport::Transport(napi_env env) : m_env(env)
{
    m_stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_readEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_syncEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    napi_value resourceName = nullptr;
    NMS_NAPI(napi_create_string_utf8)(env, "node-media-service-transport", NAPI_AUTO_LENGTH, &resourceName);
    NMS_NAPI(napi_create_threadsafe_function)(env, nullptr, nullptr, resourceName, 0, 1, nullptr, nullptr, this,
                                              transportCallJs, &m_tsfn);
}

Transport::~Transport()
{
    disconnect();
}

bool Transport::connect(const std::wstring& pipePath, DWORD timeoutMs)
{
    if (m_running.load())
    {
        return m_connected.load();
    }
    const DWORD started = ::GetTickCount();
    while (true)
    {
        const HANDLE pipe = ::CreateFileW(pipePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                          FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE)
        {
            m_pipe = pipe;
            break;
        }
        if (::GetLastError() == ERROR_PIPE_BUSY)
        {
            ::WaitNamedPipeW(pipePath.c_str(), 100);
        }
        if (timeoutMs != INFINITE && ::GetTickCount() - started >= timeoutMs)
        {
            return false;
        }
        ::Sleep(50);
    }
    m_connected.store(true);
    m_running.store(true);
    m_thread = std::thread(&Transport::readLoop, this);
    return true;
}

void Transport::disconnect()
{
    m_running.store(false);
    m_connected.store(false);
    if (m_stopEvent != nullptr)
    {
        ::SetEvent(m_stopEvent);
    }
    if (m_syncEvent != nullptr)
    {
        ::SetEvent(m_syncEvent);
    }
    if (m_pipe != INVALID_HANDLE_VALUE && m_pipe != nullptr)
    {
        ::CancelIoEx(m_pipe, nullptr);
        ::DisconnectNamedPipe(m_pipe);
    }
    if (m_thread.joinable())
    {
        m_thread.join();
    }
    if (m_pipe != INVALID_HANDLE_VALUE && m_pipe != nullptr)
    {
        ::CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }
    if (m_stopEvent != nullptr)
    {
        ::CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
    if (m_readEvent != nullptr)
    {
        ::CloseHandle(m_readEvent);
        m_readEvent = nullptr;
    }
    if (m_syncEvent != nullptr)
    {
        ::CloseHandle(m_syncEvent);
        m_syncEvent = nullptr;
    }
    if (m_tsfn != nullptr)
    {
        NMS_NAPI(napi_release_threadsafe_function)(m_tsfn, napi_tsfn_abort);
        m_tsfn = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pending.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_syncMutex);
        m_syncReplies.clear();
    }
}

uint64_t Transport::invoke(const char* serviceId, const char* method, const void* args, std::size_t argsSize,
                           napi_env env, napi_deferred deferred, InvokeCallback callback)
{
    const uint64_t requestId = m_nextRequestId.fetch_add(1);
    flatbuffers::FlatBufferBuilder builder;
    const auto serviceIdOffset = builder.CreateString(serviceId);
    const auto methodOffset = builder.CreateString(method);
    const auto argsOffset = builder.CreateVector(static_cast<const std::uint8_t*>(args), argsSize);
    const auto root = services::CreateInvokeReq(builder, services::MessageType_InvokeReq, serviceIdOffset, methodOffset,
                                                requestId, m_session.load(), argsOffset);
    builder.Finish(root);
    const std::vector<std::uint8_t> frame(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        PendingInvoke pending;
        pending.env = env;
        pending.deferred = deferred;
        pending.callback = std::move(callback);
        m_pending[requestId] = std::move(pending);
    }
    if (!writeFrame(frame))
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pending.erase(requestId);
        NMS_NAPI(napi_reject_deferred)(env, deferred, exception(env, "IPC write failed; service disconnected"));
        return 0;
    }
    return requestId;
}

std::vector<uint8_t> Transport::invokeSync(const char* serviceId, const char* method, const void* args,
                                           std::size_t argsSize, DWORD timeoutMs)
{
    const uint64_t requestId = m_nextRequestId.fetch_add(1);
    flatbuffers::FlatBufferBuilder builder;
    const auto serviceIdOffset = builder.CreateString(serviceId);
    const auto methodOffset = builder.CreateString(method);
    const auto argsOffset = builder.CreateVector(static_cast<const std::uint8_t*>(args), argsSize);
    const auto root = services::CreateInvokeReq(builder, services::MessageType_InvokeReq, serviceIdOffset, methodOffset,
                                                requestId, m_session.load(), argsOffset);
    builder.Finish(root);
    const std::vector<std::uint8_t> frame(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        PendingInvoke pending;
        pending.sync = true;
        m_pending[requestId] = std::move(pending);
    }
    if (!writeFrame(frame))
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pending.erase(requestId);
        return std::vector<uint8_t>();
    }
    const DWORD started = ::GetTickCount();
    while (true)
    {
        {
            std::lock_guard<std::mutex> lock(m_syncMutex);
            const auto it = m_syncReplies.find(requestId);
            if (it != m_syncReplies.end())
            {
                std::vector<uint8_t> result = std::move(it->second);
                m_syncReplies.erase(it);
                return result;
            }
        }
        if (!m_running.load() || !m_connected.load())
        {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            m_pending.erase(requestId);
            return std::vector<uint8_t>();
        }
        if (timeoutMs != INFINITE)
        {
            const DWORD elapsed = ::GetTickCount() - started;
            if (elapsed >= timeoutMs)
            {
                std::lock_guard<std::mutex> lock(m_pendingMutex);
                m_pending.erase(requestId);
                return std::vector<uint8_t>();
            }
            ::WaitForSingleObject(m_syncEvent, timeoutMs - elapsed);
        }
        else
        {
            ::WaitForSingleObject(m_syncEvent, INFINITE);
        }
    }
}

bool Transport::writeFrame(const std::vector<uint8_t>& frame)
{
    if (frame.size() > kMaxFrameSize)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (!m_connected.load() || m_pipe == INVALID_HANDLE_VALUE || m_pipe == nullptr)
    {
        return false;
    }
    std::uint8_t prefix[kPrefixSize] = {};
    const std::uint32_t size = static_cast<std::uint32_t>(frame.size());
    for (std::size_t byte = 0; byte < kPrefixSize; ++byte)
    {
        prefix[byte] = static_cast<std::uint8_t>(size >> (8 * byte));
    }
    DWORD written = 0;
    if (!::WriteFile(m_pipe, prefix, kPrefixSize, &written, nullptr) || written != kPrefixSize)
    {
        return false;
    }
    if (!::WriteFile(m_pipe, frame.data(), size, &written, nullptr) || written != size)
    {
        return false;
    }
    return true;
}

bool Transport::readExact(void* buffer, std::size_t size)
{
    auto* cursor = static_cast<std::uint8_t*>(buffer);
    while (size != 0)
    {
        OVERLAPPED overlapped {};
        overlapped.hEvent = m_readEvent;
        ::ResetEvent(m_readEvent);
        DWORD bytes = 0;
        const DWORD chunk = static_cast<DWORD>(size > MAXDWORD ? MAXDWORD : size);
        if (!::ReadFile(m_pipe, cursor, chunk, &bytes, &overlapped))
        {
            if (::GetLastError() != ERROR_IO_PENDING)
            {
                return false;
            }
            const HANDLE events[] = {m_stopEvent, m_readEvent};
            const DWORD wait = ::WaitForMultipleObjects(2, events, FALSE, INFINITE);
            if (wait == WAIT_OBJECT_0)
            {
                ::CancelIoEx(m_pipe, &overlapped);
                return false;
            }
            if (!::GetOverlappedResult(m_pipe, &overlapped, &bytes, FALSE))
            {
                return false;
            }
        }
        if (bytes == 0)
        {
            return false;
        }
        cursor += bytes;
        size -= bytes;
    }
    return true;
}

void Transport::readLoop()
{
    while (m_running.load() && m_connected.load())
    {
        std::uint8_t prefix[kPrefixSize] = {};
        if (!readExact(prefix, kPrefixSize))
        {
            break;
        }
        const std::uint32_t size =
            static_cast<std::uint32_t>(prefix[0]) | (static_cast<std::uint32_t>(prefix[1]) << 8) |
            (static_cast<std::uint32_t>(prefix[2]) << 16) | (static_cast<std::uint32_t>(prefix[3]) << 24);
        if (size > kMaxFrameSize)
        {
            break;
        }
        std::vector<std::uint8_t> frame(size);
        if (!readExact(frame.data(), size))
        {
            break;
        }
        if (!m_running.load() || !m_connected.load())
        {
            break;
        }
        if (frame.size() < kMinFlatBufferSize)
        {
            continue;
        }
        const services::MessageType type = services::GetInvokeReq(frame.data())->type();
        if (type == services::MessageType_InvokeRsp)
        {
            flatbuffers::Verifier verifier(frame.data(), frame.size());
            if (!services::VerifyInvokeRspBuffer(verifier))
            {
                continue;
            }
            const services::InvokeRsp* reply = services::GetInvokeRsp(frame.data());
            bool sync = false;
            {
                std::lock_guard<std::mutex> lock(m_pendingMutex);
                const auto it = m_pending.find(reply->request_id());
                if (it != m_pending.end() && it->second.sync)
                {
                    sync = true;
                    m_pending.erase(it);
                }
            }
            if (sync)
            {
                std::lock_guard<std::mutex> lock(m_syncMutex);
                m_syncReplies[reply->request_id()] = frame;
                if (m_syncEvent != nullptr)
                {
                    ::SetEvent(m_syncEvent);
                }
                continue;
            }
        }
        else if (type != services::MessageType_Event)
        {
            continue;
        }
        DispatchItem* item = new DispatchItem();
        item->isReply = (type == services::MessageType_InvokeRsp);
        item->frame = std::move(frame);
        if (NMS_NAPI(napi_call_threadsafe_function)(m_tsfn, item, napi_tsfn_blocking) != napi_ok)
        {
            delete item;
            break;
        }
    }
    m_connected.store(false);
    if (m_stopEvent != nullptr)
    {
        ::SetEvent(m_stopEvent);
    }
    if (m_syncEvent != nullptr)
    {
        ::SetEvent(m_syncEvent);
    }
}

void Transport::callJs(napi_env env, DispatchItem* item)
{
    if (item == nullptr)
    {
        return;
    }
    const bool isReply = item->isReply;
    std::vector<std::uint8_t> frame = std::move(item->frame);
    delete item;
    try
    {
        if (isReply)
        {
            flatbuffers::Verifier verifier(frame.data(), frame.size());
            if (!services::VerifyInvokeRspBuffer(verifier))
            {
                return;
            }
            const services::InvokeRsp* reply = services::GetInvokeRsp(frame.data());
            PendingInvoke pending;
            {
                std::lock_guard<std::mutex> lock(m_pendingMutex);
                const auto it = m_pending.find(reply->request_id());
                if (it == m_pending.end())
                {
                    return;
                }
                pending = std::move(it->second);
                m_pending.erase(it);
            }
            if (pending.callback)
            {
                pending.callback(pending.env, pending.deferred, reply);
            }
            return;
        }
        flatbuffers::Verifier verifier(frame.data(), frame.size());
        if (!services::VerifyServiceEventBuffer(verifier))
        {
            return;
        }
        const services::ServiceEvent* event = services::GetServiceEvent(frame.data());
        if (event->service_id() == nullptr || event->event() == nullptr)
        {
            return;
        }
        const std::string key = eventKey(event->service_id()->str().c_str(), event->event()->str().c_str());
        EventCallback callback;
        {
            std::lock_guard<std::mutex> lock(m_eventMutex);
            const auto it = m_events.find(key);
            if (it != m_events.end())
            {
                callback = it->second;
            }
        }
        if (!callback)
        {
            return;
        }
        std::vector<std::uint8_t> payload;
        if (event->payload() != nullptr)
        {
            payload.assign(event->payload()->begin(), event->payload()->end());
        }
        callback(env, payload);
    }
    catch (...)
    {
    }
}

void Transport::setEventCallback(const char* serviceId, const char* event, EventCallback callback)
{
    const std::string key = eventKey(serviceId, event);
    std::lock_guard<std::mutex> lock(m_eventMutex);
    if (callback)
    {
        m_events[key] = std::move(callback);
    }
    else
    {
        m_events.erase(key);
    }
}
} // namespace node_media_service
