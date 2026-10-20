#pragma once

#include "protos/schema_traits.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class IpcServer;
class EventLoop;

// Service 基类：media-service 内的每个业务一个 Service，类名即 service_id。
// Service 只定义方法（invoke）与事件（emitEvent）的接口与回执；
// addon 侧由 schemas 生成同名映射类（ServiceProxy 子类），两端名字一致。
class Service
{
public:
    // 统一工厂签名：services::registerAll 通过它构造每个 Service（无依赖的服务可忽略参数）。
    using Factory = std::shared_ptr<Service> (*)(IpcServer* server, EventLoop* eventLoop);

    // invoke 回执：success=false 时 message 说明失败原因；
    // resultBytes 为方法返回表（如 CreateResult）的 FlatBuffer 字节，可为空。
    struct ReplyWriter
    {
        std::function<void(bool, const std::string&, const std::vector<std::uint8_t>&)> write;
    };

    virtual ~Service() = default;

    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;

    // 类名即 service_id，两端一致（如 "NativeWindowService"）。
    virtual const char* serviceId() const = 0;

    // 分发一个方法调用；args 为 Args 表的 FlatBuffer 字节。
    // 返回 false 表示不识别该方法（调用方按失败回执处理）。
    virtual bool invoke(const char* method, const std::uint8_t* args, std::size_t argsSize, ReplyWriter& reply) = 0;

    // 应用退出前的生命周期收口（ServiceRegistry::closeAll 统一调用，默认空实现）。
    virtual void closeAll()
    {
    }

protected:
    Service() = default;

    // 成功回执（无返回表字节）。
    static void replyOk(ReplyWriter& reply)
    {
        reply.write(true, "", {});
    }

    // 失败回执。
    static void replyError(ReplyWriter& reply, const std::string& message)
    {
        reply.write(false, message, {});
    }

    // 成功回执 + 方法返回表字节（如 HelloResult）；build 负责创建 Result 表。
    template <typename ResultT, typename Fn>
    static void replyResult(ReplyWriter& reply, Fn&& build)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto body = build(builder);
        builder.Finish(body);
        reply.write(
            true, "",
            std::vector<std::uint8_t>(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()));
    }
};

// 模板化 Service 实现基类（CRTP）：invoke 按方法名路由到类型化 handler，
// Args 解码/校验、回执与事件发射全部收敛到基类，派生类只写业务逻辑。
// 派生类只需：
//   1. 构造函数里 this->registerMethod<ArgsT>("method", &Derived::method)；
//   2. 实现 bool method(const ArgsT& args, ReplyWriter& reply)；
//   3. 回执用 replyOk / replyError / replyResult<ResultT>，事件用 this->emit<EventT>。
// 方法名与 fbs 方法表一一对应（如 "create" -> CreateArgs），ArgsT 必须是 root 表
// （services::traits::Root<ArgsT> 提供 name/verify/root）。
template <typename Derived>
class ServiceBase : public Service
{
public:
    // server 用于事件发射（IpcServer::emitEvent）；不发射事件的服务可传 nullptr。
    explicit ServiceBase(IpcServer* server = nullptr) : m_server(server)
    {
    }

    bool invoke(const char* method, const std::uint8_t* args, std::size_t argsSize, ReplyWriter& reply) override
    {
        if (method == nullptr)
        {
            return false;
        }
        const auto it = m_methods.find(method);
        if (it == m_methods.end())
        {
            return false;
        }
        return it->second(args, argsSize, reply);
    }

protected:
    IpcServer* server() const
    {
        return m_server;
    }

    // 注册一个方法；handler = bool (Derived::*)(const ArgsT&, ReplyWriter&)。
    // 方法名必须与 addon 侧生成代理的方法名一致。
    template <typename ArgsT>
    void registerMethod(const char* name, bool (Derived::*handler)(const ArgsT&, ReplyWriter&))
    {
        using Traits = services::traits::Root<ArgsT>;
        m_methods[name] = [this, handler](const std::uint8_t* args, std::size_t argsSize, ReplyWriter& reply) -> bool
        {
            if (args == nullptr || argsSize == 0)
            {
                reply.write(false, std::string("Missing ") + Traits::name(), {});
                return true;
            }
            if (!Traits::verify(args, argsSize))
            {
                reply.write(false, std::string("Invalid ") + Traits::name(), {});
                return true;
            }
            return (static_cast<Derived*>(this)->*handler)(*Traits::root(args), reply);
        };
    }

    // 事件发射：构造 EventT 表字节并经 IpcServer::emitEvent 推送（线程安全）。
    template <typename EventT, typename Fn>
    void emit(const char* event, Fn&& build)
    {
        if (m_server == nullptr)
        {
            return;
        }
        flatbuffers::FlatBufferBuilder builder;
        const auto body = build(builder);
        builder.Finish(body);
        const std::vector<std::uint8_t> payload(builder.GetBufferPointer(),
                                                builder.GetBufferPointer() + builder.GetSize());
        m_server->emitEvent(serviceId(), event, payload);
    }

private:
    using MethodFn = std::function<bool(const std::uint8_t*, std::size_t, ReplyWriter&)>;

    std::map<std::string, MethodFn> m_methods;
    IpcServer* m_server;
};
