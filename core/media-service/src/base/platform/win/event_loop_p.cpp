#include "base/event_loop_p.h"

#include "base/logger.h"

#include <exception>
#include <string>
#include <utility>

namespace
{

const char kComponent[] = "event-loop";
const wchar_t kHostClass[] = L"FolioEventLoopHost";
constexpr UINT kMsgWake = WM_APP + 1;

// 日志失败也不能把异常带出 Win32 的窗口过程。
void logError(const char* message, const char* detail = nullptr) noexcept
{
    try
    {
        std::string text(message);
        if (detail != nullptr)
        {
            text += ": ";
            text += detail;
        }
        mediaservice::logger().logError(kComponent, text);
    }
    catch (...)
    {
        ::OutputDebugStringA("[event-loop] failed to write error log\n");
    }
}

void logWindowsError(const char* message, DWORD error) noexcept
{
    try
    {
        const std::string detail = std::to_string(error);
        logError(message, detail.c_str());
    }
    catch (...)
    {
        logError(message);
    }
}

} // namespace

EventLoopPrivate::~EventLoopPrivate()
{
    stop();
}

bool EventLoopPrivate::initialize()
{
    const DWORD threadId = ::GetCurrentThreadId();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_running)
        {
            return false;
        }
        if (m_hostHwnd != nullptr)
        {
            return m_threadId == threadId;
        }
    }

    // 原生窗口沿用 per-monitor v2；已由宿主/清单设置时失败也不影响启动。
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW hostClass {};
    hostClass.cbSize = sizeof(hostClass);
    hostClass.lpfnWndProc = hostWndProc;
    hostClass.hInstance = ::GetModuleHandleW(nullptr);
    hostClass.lpszClassName = kHostClass;
    if (::RegisterClassExW(&hostClass) == 0)
    {
        const DWORD error = ::GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS)
        {
            logWindowsError("RegisterClassEx(host) failed", error);
            return false;
        }
    }

    const HWND hwnd =
        ::CreateWindowExW(0, kHostClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, ::GetModuleHandleW(nullptr), this);
    if (hwnd == nullptr)
    {
        logWindowsError("CreateWindowEx(host) failed", ::GetLastError());
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_hostHwnd = hwnd;
    m_threadId = threadId;
    m_stopped = false;
    m_quitRequested = false;
    m_failed = false;
    return true;
}

bool EventLoopPrivate::run()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopped || m_running || m_threadId != ::GetCurrentThreadId())
        {
            return false;
        }
        m_running = true;
    }

    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_quitRequested || m_stopped)
            {
                break;
            }
        }

        MSG message {};
        const BOOL result = ::GetMessageW(&message, nullptr, 0, 0);
        if (result == -1)
        {
            logWindowsError("GetMessage failed", ::GetLastError());
            fail();
            break;
        }
        if (result == 0)
        {
            requestQuit();
            break;
        }

        // 退出优先于队列中的唤醒消息，不执行退出请求之后尚未开始的任务。
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_quitRequested || m_stopped)
            {
                break;
            }
        }
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_running = false;
    m_quitRequested = true;
    return !m_failed;
}

bool EventLoopPrivate::postTask(std::function<void()> task)
{
    if (!task)
    {
        return false;
    }

    DWORD error = ERROR_SUCCESS;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopped || m_quitRequested || m_hostHwnd == nullptr)
        {
            return false;
        }
        m_tasks.push_back(std::move(task));
        if (::PostMessageW(m_hostHwnd, kMsgWake, 0, 0) != FALSE)
        {
            return true;
        }
        error = ::GetLastError();
        // 持锁投递与回滚：返回 false 的任务不会留在队列里等待其他消息。
        task = std::move(m_tasks.back());
        m_tasks.pop_back();
    }
    logWindowsError("PostMessage(task) failed", error);
    return false;
}

void EventLoopPrivate::requestQuit()
{
    DWORD error = ERROR_SUCCESS;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopped || m_quitRequested)
        {
            return;
        }
        m_quitRequested = true;
        if (::PostMessageW(m_hostHwnd, kMsgWake, 0, 0) != FALSE)
        {
            return;
        }
        error = ::GetLastError();
        // 初始化已创建线程消息队列；窗口唤醒失败时再尝试线程消息。
        if (::PostThreadMessageW(m_threadId, kMsgWake, 0, 0) != FALSE)
        {
            return;
        }
    }
    logWindowsError("failed to wake event loop for quit", error);
}

void EventLoopPrivate::stop()
{
    HWND hwnd = nullptr;
    std::deque<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopped)
        {
            return;
        }
        if (m_threadId != ::GetCurrentThreadId())
        {
            logError("stop must be called on the UI thread");
            return;
        }
        m_stopped = true;
        m_quitRequested = true;
        hwnd = m_hostHwnd;
        m_hostHwnd = nullptr;
        m_threadId = 0;
        tasks.swap(m_tasks);
    }

    // 不产生 WM_QUIT，以便同一线程可以重新初始化并运行新一轮循环。
    if (hwnd != nullptr && ::DestroyWindow(hwnd) == FALSE)
    {
        logWindowsError("DestroyWindow(host) failed", ::GetLastError());
    }
}

bool EventLoopPrivate::isCurrentThread() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_stopped && m_threadId == ::GetCurrentThreadId();
}

void EventLoopPrivate::runTasks()
{
    std::deque<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running || m_stopped || m_quitRequested)
        {
            return;
        }
        tasks.swap(m_tasks);
    }

    for (auto& task : tasks)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopped || m_quitRequested)
            {
                return;
            }
        }
        try
        {
            task();
        }
        catch (const std::exception& error)
        {
            logError("UI task failed", error.what());
            fail();
            return;
        }
        catch (...)
        {
            logError("UI task failed with an unknown exception");
            fail();
            return;
        }
    }
}

void EventLoopPrivate::fail()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_failed = true;
    }
    requestQuit();
}

LRESULT CALLBACK EventLoopPrivate::hostWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    auto* self = reinterpret_cast<EventLoopPrivate*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<EventLoopPrivate*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    try
    {
        switch (message)
        {
        case kMsgWake:
            if (self != nullptr)
            {
                self->runTasks();
            }
            return 0;
        case WM_CLOSE:
            if (self != nullptr)
            {
                self->requestQuit();
            }
            return 0;
        case WM_NCDESTROY:
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            break;
        default:
            break;
        }
    }
    catch (const std::exception& error)
    {
        logError("UI window procedure failed", error.what());
        if (self != nullptr)
        {
            self->fail();
        }
        return 0;
    }
    catch (...)
    {
        logError("UI window procedure failed with an unknown exception");
        if (self != nullptr)
        {
            self->fail();
        }
        return 0;
    }
    return ::DefWindowProcW(hwnd, message, wparam, lparam);
}
