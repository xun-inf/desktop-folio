#include "base/ipc_server_p.h"

#include "base/logger.h"
#include "utils/stringutil.h"

#include <exception>

namespace
{

const char kComponent[] = "ipc";
constexpr DWORD kPipeBufferSize = 64 * 1024;
constexpr std::size_t kPrefixSize = 4;
constexpr std::uint32_t kMaxFrameSize = 8 * 1024 * 1024;

void logError(const char* message, DWORD error = ERROR_SUCCESS) noexcept
{
    try
    {
        std::string text(message);
        if (error != ERROR_SUCCESS)
            text += ": " + liveutils::lastErrorMessage(error);
        mediaservice::logger().logError(kComponent, text);
    }
    catch (...)
    {
        ::OutputDebugStringA("[ipc] failed to write error log\n");
    }
}

void logInfo(const char* message) noexcept
{
    try
    {
        mediaservice::logger().logInfo(kComponent, message);
    }
    catch (...)
    {
        ::OutputDebugStringA("[ipc] failed to write log\n");
    }
}

bool aborted(HANDLE stopEvent, HANDLE disconnectEvent)
{
    const HANDLE events[] = {stopEvent, disconnectEvent};
    return ::WaitForMultipleObjects(2, events, FALSE, 0) != WAIT_TIMEOUT;
}

// 取消后等待终态，保证栈上的 OVERLAPPED 和调用方缓冲区不会早于 I/O 释放。
bool waitForIo(HANDLE pipe, OVERLAPPED* overlapped, HANDLE stopEvent, HANDLE disconnectEvent, DWORD* bytes,
               DWORD timeout = INFINITE)
{
    const HANDLE events[] = {stopEvent, disconnectEvent, overlapped->hEvent};
    const DWORD wait = ::WaitForMultipleObjects(3, events, FALSE, timeout);
    if (wait == WAIT_OBJECT_0 + 2)
    {
        return ::GetOverlappedResult(pipe, overlapped, bytes, FALSE) != FALSE;
    }
    const DWORD error = wait == WAIT_FAILED ? ::GetLastError() : ERROR_OPERATION_ABORTED;
    ::CancelIoEx(pipe, overlapped);
    DWORD ignored = 0;
    ::GetOverlappedResult(pipe, overlapped, &ignored, TRUE);
    ::SetLastError(error);
    return false;
}

bool readExact(HANDLE pipe, HANDLE ioEvent, HANDLE stopEvent, HANDLE disconnectEvent, void* buffer, std::size_t size)
{
    auto* cursor = static_cast<std::uint8_t*>(buffer);
    while (size != 0)
    {
        if (aborted(stopEvent, disconnectEvent))
            return false;
        OVERLAPPED overlapped {};
        overlapped.hEvent = ioEvent;
        if (!::ResetEvent(ioEvent))
            return false;
        DWORD bytes = 0;
        const DWORD chunk = static_cast<DWORD>(size > MAXDWORD ? MAXDWORD : size);
        if (!::ReadFile(pipe, cursor, chunk, &bytes, &overlapped))
        {
            if (::GetLastError() != ERROR_IO_PENDING ||
                !waitForIo(pipe, &overlapped, stopEvent, disconnectEvent, &bytes))
            {
                return false;
            }
        }
        if (bytes == 0)
            return false;
        cursor += bytes;
        size -= bytes;
    }
    return true;
}

bool writeExact(HANDLE pipe, HANDLE ioEvent, HANDLE stopEvent, HANDLE disconnectEvent, const void* buffer,
                std::size_t size, uint64_t deadline)
{
    const auto* cursor = static_cast<const std::uint8_t*>(buffer);
    while (size != 0)
    {
        if (aborted(stopEvent, disconnectEvent))
            return false;
        OVERLAPPED overlapped {};
        overlapped.hEvent = ioEvent;
        if (!::ResetEvent(ioEvent))
            return false;
        DWORD bytes = 0;
        const DWORD chunk = static_cast<DWORD>(size > MAXDWORD ? MAXDWORD : size);
        const uint64_t tick = ::GetTickCount64();
        if (tick >= deadline)
        {
            ::SetLastError(ERROR_TIMEOUT);
            return false;
        }
        if (!::WriteFile(pipe, cursor, chunk, &bytes, &overlapped))
        {
            if (::GetLastError() != ERROR_IO_PENDING ||
                !waitForIo(pipe, &overlapped, stopEvent, disconnectEvent, &bytes, static_cast<DWORD>(deadline - tick)))
            {
                return false;
            }
        }
        if (bytes == 0)
            return false;
        cursor += bytes;
        size -= bytes;
    }
    return true;
}

bool readFrame(HANDLE pipe, HANDLE ioEvent, HANDLE stopEvent, HANDLE disconnectEvent, std::vector<std::uint8_t>* frame)
{
    std::uint8_t prefix[kPrefixSize] = {};
    if (!readExact(pipe, ioEvent, stopEvent, disconnectEvent, prefix, kPrefixSize))
        return false;
    const std::uint32_t size = static_cast<std::uint32_t>(prefix[0]) | (static_cast<std::uint32_t>(prefix[1]) << 8) |
                               (static_cast<std::uint32_t>(prefix[2]) << 16) |
                               (static_cast<std::uint32_t>(prefix[3]) << 24);
    if (size > kMaxFrameSize)
    {
        logError("incoming frame exceeds 8 MiB");
        return false;
    }
    frame->resize(size);
    return readExact(pipe, ioEvent, stopEvent, disconnectEvent, frame->data(), size);
}

bool writeFrame(HANDLE pipe, HANDLE ioEvent, HANDLE stopEvent, HANDLE disconnectEvent,
                const std::vector<std::uint8_t>& frame, uint64_t deadline)
{
    const auto size = static_cast<std::uint32_t>(frame.size());
    std::uint8_t prefix[kPrefixSize] = {};
    for (std::size_t byte = 0; byte < kPrefixSize; ++byte)
    {
        prefix[byte] = static_cast<std::uint8_t>(size >> (8 * byte));
    }
    return writeExact(pipe, ioEvent, stopEvent, disconnectEvent, prefix, kPrefixSize, deadline) &&
           writeExact(pipe, ioEvent, stopEvent, disconnectEvent, frame.data(), frame.size(), deadline);
}

bool isConnectionError(DWORD error)
{
    return error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA || error == ERROR_PIPE_NOT_CONNECTED ||
           error == ERROR_OPERATION_ABORTED;
}

} // namespace

IpcServerPrivate::~IpcServerPrivate()
{
    stop();
}

bool IpcServerPrivate::start(const std::string& pipeName)
{
    if (m_running.load() || (m_thread.joinable() && m_thread.get_id() == std::this_thread::get_id()))
    {
        return false;
    }
    stop();
    struct Rollback
    {
        IpcServerPrivate& server;
        HANDLE firstPipe = INVALID_HANDLE_VALUE;
        bool active = true;
        explicit Rollback(IpcServerPrivate& target) : server(target)
        {
        }
        ~Rollback()
        {
            if (!active)
                return;
            if (firstPipe != INVALID_HANDLE_VALUE)
                ::CloseHandle(firstPipe);
            server.stop();
        }
    } rollback {*this};
    try
    {
        m_pipeName = pipeName;
        m_stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        m_disconnectEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        m_readEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        m_writeEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (m_stopEvent == nullptr || m_disconnectEvent == nullptr || m_readEvent == nullptr || m_writeEvent == nullptr)
        {
            logError("failed to create pipe events", ::GetLastError());
            return false;
        }
        rollback.firstPipe = createPipe();
        if (rollback.firstPipe == INVALID_HANDLE_VALUE)
            return false;
        m_running.store(true);
        m_thread = std::thread(&IpcServerPrivate::acceptLoop, this, rollback.firstPipe);
        // 线程创建成功才转移首个管道；构造失败时仍由 rollback 关闭。
        rollback.firstPipe = INVALID_HANDLE_VALUE;
        rollback.active = false;
    }
    catch (const std::exception&)
    {
        logError("failed to start pipe server");
        return false;
    }
    catch (...)
    {
        logError("failed to start pipe server with an unknown exception");
        return false;
    }
    logInfo("pipe server started");
    return true;
}

void IpcServerPrivate::stop()
{
    if (m_thread.joinable() && m_thread.get_id() == std::this_thread::get_id())
    {
        logError("stop must be called outside IPC callbacks");
        return;
    }
    m_running.store(false);
    m_connected.store(false);
    if (m_stopEvent != nullptr)
        ::SetEvent(m_stopEvent);
    // 致命错误可能已将 running 置 false，仍然必须等待接收线程退出。
    if (m_thread.joinable())
        m_thread.join();
    {
        std::lock_guard<std::mutex> lock(m_sendMutex);
        if (m_stopEvent != nullptr)
            ::CloseHandle(m_stopEvent);
        if (m_disconnectEvent != nullptr)
            ::CloseHandle(m_disconnectEvent);
        if (m_readEvent != nullptr)
            ::CloseHandle(m_readEvent);
        if (m_writeEvent != nullptr)
            ::CloseHandle(m_writeEvent);
        m_stopEvent = nullptr;
        m_disconnectEvent = nullptr;
        m_readEvent = nullptr;
        m_writeEvent = nullptr;
        m_pipe = INVALID_HANDLE_VALUE;
        m_connected.store(false);
    }
}

HANDLE
IpcServerPrivate::createPipe()
{
    const std::wstring pipePath = liveutils::utf8ToWide(m_pipeName);
    const HANDLE pipe = ::CreateNamedPipeW(pipePath.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                           PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, kPipeBufferSize,
                                           kPipeBufferSize, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
        logError("CreateNamedPipe failed", ::GetLastError());
    return pipe;
}

bool IpcServerPrivate::connectClient(HANDLE pipe, DWORD* error)
{
    *error = ERROR_SUCCESS;
    if (aborted(m_stopEvent, m_disconnectEvent))
        return false;
    OVERLAPPED overlapped {};
    overlapped.hEvent = m_readEvent;
    if (!::ResetEvent(m_readEvent))
    {
        *error = ::GetLastError();
        return false;
    }
    if (::ConnectNamedPipe(pipe, &overlapped))
        return true;
    *error = ::GetLastError();
    if (*error == ERROR_PIPE_CONNECTED)
        return true;
    if (*error != ERROR_IO_PENDING)
        return false;
    DWORD ignored = 0;
    if (!waitForIo(pipe, &overlapped, m_stopEvent, m_disconnectEvent, &ignored))
    {
        *error = ::GetLastError();
        return false;
    }
    *error = ERROR_SUCCESS;
    return true;
}

void IpcServerPrivate::readLoop(HANDLE pipe)
{
    while (m_running.load() && m_connected.load())
    {
        std::vector<std::uint8_t> frame;
        if (!readFrame(pipe, m_readEvent, m_stopEvent, m_disconnectEvent, &frame))
            break;
        if (m_running.load() && m_connected.load())
        {
            // 客户端只会发送 InvokeReq；无法解析的消息按协议错误断开连接。
            if (!dispatchInvoke(frame))
            {
                logError("malformed frame; closing connection");
                break;
            }
        }
    }
}

void IpcServerPrivate::notifyDisconnected() noexcept
{
    try
    {
        if (m_disconnectCallback)
            m_disconnectCallback();
    }
    catch (const std::exception&)
    {
        logError("disconnect callback failed");
    }
    catch (...)
    {
        logError("disconnect callback failed with an unknown exception");
    }
}

void IpcServerPrivate::acceptLoop(HANDLE firstPipe) noexcept
{
    HANDLE pipe = firstPipe;
    bool fatal = false;
    try
    {
        while (m_running.load())
        {
            if (pipe == INVALID_HANDLE_VALUE)
            {
                pipe = createPipe();
                if (pipe == INVALID_HANDLE_VALUE)
                {
                    fatal = true;
                    break;
                }
            }
            ::ResetEvent(m_disconnectEvent);
            {
                std::lock_guard<std::mutex> lock(m_sendMutex);
                m_pipe = pipe;
            }
            DWORD error = ERROR_SUCCESS;
            if (!connectClient(pipe, &error))
            {
                closePipe(pipe);
                pipe = INVALID_HANDLE_VALUE;
                if (m_running.load() && !isConnectionError(error))
                {
                    logError("ConnectNamedPipe failed", error);
                    fatal = true;
                    break;
                }
                continue;
            }
            if (!m_running.load())
                break;
            m_connected.store(true);
            logInfo("client connected");
            readLoop(pipe);
            m_connected.store(false);
            closePipe(pipe);
            pipe = INVALID_HANDLE_VALUE;
            logInfo("client disconnected");
            if (m_running.load())
                notifyDisconnected();
        }
    }
    catch (const std::exception&)
    {
        logError("pipe receive thread failed");
        fatal = true;
    }
    catch (...)
    {
        logError("pipe receive thread failed with an unknown exception");
        fatal = true;
    }
    m_connected.store(false);
    if (fatal)
    {
        m_running.store(false);
        ::SetEvent(m_stopEvent);
    }
    if (pipe != INVALID_HANDLE_VALUE)
        closePipe(pipe);
    if (fatal)
        notifyDisconnected();
    logInfo("pipe server loop exited");
}

bool IpcServerPrivate::send(const std::vector<uint8_t>& message)
{
    if (message.size() > kMaxFrameSize)
        return false;
    const uint64_t deadline = ::GetTickCount64() + 2000;
    std::unique_lock<std::mutex> lock(m_sendMutex, std::defer_lock);
    while (!lock.try_lock())
    {
        if (aborted(m_stopEvent, m_disconnectEvent) || ::GetTickCount64() >= deadline)
            return false;
        ::WaitForSingleObject(m_stopEvent, 1);
    }
    if (!m_running.load() || !m_connected.load() || m_pipe == INVALID_HANDLE_VALUE)
        return false;
    if (!writeFrame(m_pipe, m_writeEvent, m_stopEvent, m_disconnectEvent, message, deadline))
    {
        // 即使只写出长度前缀也不能复用该流，否则下一帧会被读成旧帧的内容。
        m_connected.store(false);
        ::SetEvent(m_disconnectEvent);
        ::CancelIoEx(m_pipe, nullptr);
        ::DisconnectNamedPipe(m_pipe);
        logError("send failed; current pipe connection closed");
        return false;
    }
    return true;
}

void IpcServerPrivate::closePipe(HANDLE pipe)
{
    // 先唤醒正在等待的外部 send，再等写锁，避免坏帧/断线收尾卡在写入上。
    ::SetEvent(m_disconnectEvent);
    ::CancelIoEx(pipe, nullptr);
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (m_pipe == pipe)
        m_pipe = INVALID_HANDLE_VALUE;
    ::DisconnectNamedPipe(pipe);
    ::CloseHandle(pipe);
}
