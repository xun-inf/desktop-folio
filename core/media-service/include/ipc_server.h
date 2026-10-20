#pragma once

#include "protos/service.h"

#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class EventLoop;
class IpcServerPrivate;
class ServiceRegistry;

// 客户端断开或监听失败时在 IPC 线程上调用；不要在回调里调用 stop。
using DisconnectCallback = std::function<void()>;

// Named Pipe 服务端 + 通用消息路由：InvokeReq -> ServiceRegistry，
// InvokeRsp/Event 由 Service 经 reply()/emitEvent() 构造发送。
class IpcServer
{
    std::unique_ptr<IpcServerPrivate> d_ptr;

public:
    // 回调在构造时绑定且保持不变，可为空。
    explicit IpcServer(DisconnectCallback callback = {});
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    bool connected() const;

    // 应用生命周期线程调用一次：设置 Service 路由与 UI 线程任务队列。
    // 必须在 start() 之前调用。
    void setRegistry(ServiceRegistry* registry);
    void setEventLoop(EventLoop* eventLoop);

    // start/stop 由应用生命周期线程串行调用；成功表示监听端点已创建。
    bool start(const std::string& pipeName);
    void stop();

    // 线程安全，按完整帧串行同步发送；连接未建立或正在停机时返回 false。
    bool send(const std::vector<uint8_t>& msg);

    // 会话标识：hello 握手成功后设置，后续 InvokeReq 必须携带相同值。
    uint64_t session() const
    {
        return m_session.load();
    }
    void setSession(uint64_t value)
    {
        m_session.store(value);
    }

    // 线程安全：构造 InvokeRsp 并发送。session 取自当前会话（hello 时服务
    // 先 setSession 再 reply，回执即携带新会话）。
    bool reply(uint64_t requestId, bool success, const std::string& message, const std::vector<uint8_t>& resultBytes);

    // 线程安全：构造 Event 并发送。
    bool emitEvent(const std::string& serviceId, const std::string& event, const std::vector<uint8_t>& payloadBytes);

private:
    std::atomic<uint64_t> m_session {0};
};
