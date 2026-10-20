#pragma once

#include "ipc_server.h"

#include <atomic>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#endif

class IpcServerPrivate
{
public:
    IpcServerPrivate(IpcServer* owner, DisconnectCallback callback);
    ~IpcServerPrivate();

    IpcServerPrivate(const IpcServerPrivate&) = delete;
    IpcServerPrivate& operator=(const IpcServerPrivate&) = delete;

    void setRegistry(ServiceRegistry* registry);
    void setEventLoop(EventLoop* eventLoop);
    bool connected() const;
    bool start(const std::string& pipeName);
    void stop();
    bool send(const std::vector<uint8_t>& message);

private:
    // IPC 线程：解析 InvokeReq，校验会话，分派到 Service。
    // SessionService 的 hello/shutdown 在 IPC 线程同步处理（连接期握手），
    // 其余 Service 通过 EventLoop::postTask 排到 UI 线程执行。
    bool dispatchInvoke(const std::vector<uint8_t>& frame) noexcept;

    // 线程安全：构造 InvokeRsp 并发送（与 IpcServer::reply 相同语义）。
    bool reply(uint64_t requestId, bool success, const std::string& message, const std::vector<uint8_t>& resultBytes);

#if defined(_WIN32)
    HANDLE createPipe();
    void acceptLoop(HANDLE firstPipe) noexcept;
    bool connectClient(HANDLE pipe, DWORD* error);
    void readLoop(HANDLE pipe);
    void closePipe(HANDLE pipe);
    void notifyDisconnected() noexcept;

    HANDLE m_pipe = INVALID_HANDLE_VALUE;
    HANDLE m_stopEvent = nullptr;
    HANDLE m_disconnectEvent = nullptr;
    HANDLE m_readEvent = nullptr;
    HANDLE m_writeEvent = nullptr;
#endif

    std::string m_pipeName;
    std::thread m_thread;
    std::atomic<bool> m_running {false};
    std::atomic<bool> m_connected {false};
    // 收发使用独立 OVERLAPPED；该锁只串行化完整帧写入与管道释放。
    std::mutex m_sendMutex;
    const DisconnectCallback m_disconnectCallback;

    // 路由目标；start() 前由应用生命周期线程设置一次，之后不再改变。
    IpcServer* m_owner = nullptr;
    ServiceRegistry* m_registry = nullptr;
    EventLoop* m_eventLoop = nullptr;
};
