#pragma once

#include "protos/service.h"

#include <node_api.h>
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace node_media_service
{
// 消息类型：core/schemas/service.fbs 的 MessageType 枚举（None=0 / InvokeReq=1 / InvokeRsp=2 / Event=3）。

// invoke 回执回调：在 JS 线程调用（经 TSFN），可安全 resolve/reject deferred。
using InvokeCallback = std::function<void(napi_env, napi_deferred, const services::InvokeRsp*)>;
// 事件回调：在 JS 线程调用，payload 为事件表（如 FrameEvent）的 FlatBuffer 字节。
using EventCallback = std::function<void(napi_env, const std::vector<uint8_t>&)>;

// 登记中的 invoke：invoke() 在 JS 线程登记，callJs() 取回后在 JS 线程 settle。
struct PendingInvoke
{
    napi_env env = nullptr;
    napi_deferred deferred = nullptr;
    InvokeCallback callback;
    // 同步握手模式：读线程直接把 InvokeRsp 存入 syncReplies，不走 TSFN（避免 JS 线程被阻塞时死锁）。
    bool sync = false;
};

// 读线程 -> JS 线程的投递项；由 callJs 释放。
struct DispatchItem
{
    bool isReply = false;
    std::vector<uint8_t> frame;
};

// Named Pipe 客户端：发送 InvokeReq，读线程按 type 分派 InvokeRsp/ServiceEvent。
class Transport
{
public:
    explicit Transport(napi_env env);
    ~Transport();

    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    // 连接服务端管道并启动读线程；timeoutMs 内重试。
    bool connect(const std::wstring& pipePath, DWORD timeoutMs);
    void disconnect();

    bool connected() const
    {
        return m_connected.load();
    }
    DWORD childPid() const
    {
        return m_childPid;
    }
    void setChildPid(DWORD pid)
    {
        m_childPid = pid;
    }

    uint64_t session() const
    {
        return m_session.load();
    }
    void setSession(uint64_t session)
    {
        m_session.store(session);
    }

    // 发送 InvokeReq 并登记 pending；返回 request_id（0 表示发送失败，deferred 已被拒绝）。
    uint64_t invoke(const char* serviceId, const char* method, const void* args, std::size_t argsSize, napi_env env,
                    napi_deferred deferred, InvokeCallback callback);

    // 同步握手调用：阻塞等待 InvokeRsp 字节（空 vector 表示失败/超时）；仅用于连接期握手。
    std::vector<uint8_t> invokeSync(const char* serviceId, const char* method, const void* args, std::size_t argsSize,
                                    DWORD timeoutMs);

    // 订阅/取消服务端事件（ServiceEvent）；callback 为空时取消。
    void setEventCallback(const char* serviceId, const char* event, EventCallback callback);

    // TSFN 的 JS 线程入口（注册给 napi_threadsafe_function）。
    void callJs(napi_env env, DispatchItem* item);

private:
    bool writeFrame(const std::vector<uint8_t>& frame);
    bool readExact(void* buffer, std::size_t size);
    void readLoop();

    napi_env m_env = nullptr;
    napi_threadsafe_function m_tsfn = nullptr;
    HANDLE m_pipe = INVALID_HANDLE_VALUE;
    HANDLE m_readEvent = nullptr;
    HANDLE m_stopEvent = nullptr;
    HANDLE m_syncEvent = nullptr;
    std::thread m_thread;
    std::atomic<bool> m_running {false};
    std::atomic<bool> m_connected {false};
    std::atomic<uint64_t> m_session {0};
    std::atomic<uint64_t> m_nextRequestId {1};
    DWORD m_childPid = 0;

    std::mutex m_pendingMutex;
    std::map<uint64_t, PendingInvoke> m_pending;
    std::mutex m_syncMutex;
    std::map<uint64_t, std::vector<uint8_t>> m_syncReplies;
    std::mutex m_eventMutex;
    std::map<std::string, EventCallback> m_events;
    std::mutex m_sendMutex;
};
} // namespace node_media_service
