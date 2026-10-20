#include "services/native_window/native_window_p.h"

#include "base/logger.h"

#include <cstdio>
#include <string>

namespace
{

const char kComponent[] = "nativewindow";
const wchar_t kViewClass[] = L"NativeWindowView";

LRESULT CALLBACK viewWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_NCHITTEST:
        // 整块穿透：点击落到下面的 Electron 主窗口上，交给 Chromium 决定怎么响应
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    default:
        return ::DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

bool ensureViewClass()
{
    static bool registered = false;
    if (registered)
    {
        return true;
    }
    WNDCLASSEXW viewClass {};
    viewClass.cbSize = sizeof(viewClass);
    viewClass.lpfnWndProc = viewWndProc;
    viewClass.hInstance = ::GetModuleHandleW(nullptr);
    viewClass.lpszClassName = kViewClass;
    viewClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    // 画面管线还没接：view 先是一块黑底，擦背景和重绘都交给 DefWindowProc
    viewClass.hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
    if (::RegisterClassExW(&viewClass) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return false;
    }
    registered = true;
    return true;
}

std::string hex(uint64_t value)
{
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(value));
    return buffer;
}

} // namespace

NativeWindowPrivate::NativeWindowPrivate() = default;

NativeWindowPrivate::~NativeWindowPrivate()
{
    destroyView();
}

bool NativeWindowPrivate::close()
{
    const bool ok = destroyView();
    m_attached = false;
    return ok;
}

bool NativeWindowPrivate::ensureViewWindow()
{
    if (m_viewHwnd != nullptr && ::IsWindow(m_viewHwnd))
    {
        return true;
    }
    // 窗口已经不在了（owner 被销毁时会把它一起带走）：清掉残留句柄，按新建处理
    m_viewHwnd = nullptr;
    if (m_parentHwnd == nullptr || ::IsWindow(m_parentHwnd) == FALSE || !ensureViewClass())
    {
        return false;
    }
    // overlay 用 WS_POPUP 弹窗、owner 指 Electron 主窗口：Chromium 用
    // DirectComposition 渲染，WS_CHILD 子窗口压不上去，弹窗才行
    m_viewHwnd = ::CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kViewClass, L"", WS_POPUP | WS_CLIPSIBLINGS, 0,
                                   0, 1, 1, m_parentHwnd, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    if (m_viewHwnd == nullptr)
    {
        mediaservice::logger().logError(kComponent, "CreateWindowEx(view) failed: " + std::to_string(::GetLastError()));
    }
    return m_viewHwnd != nullptr;
}

bool NativeWindowPrivate::attach(uint64_t parentHandle, bool show)
{
    const HWND parent = reinterpret_cast<HWND>(static_cast<uintptr_t>(parentHandle));
    if (::IsWindow(parent) == FALSE)
    {
        mediaservice::logger().logError(kComponent, "parent hwnd is not a window: " + hex(parentHandle));
        return false;
    }
    if (m_parentHwnd != parent)
    {
        m_parentHwnd = parent;
        if (m_viewHwnd != nullptr && ::IsWindow(m_viewHwnd))
        {
            // 换 owner：弹窗的 GWLP_HWNDPARENT 存的就是 owner
            ::SetWindowLongPtrW(m_viewHwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(parent));
        }
    }
    if (!ensureViewWindow())
    {
        return false;
    }
    m_attached = true;
    ::ShowWindow(m_viewHwnd, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (!applyRect())
        return false;
    mediaservice::logger().logInfo(kComponent, "view attached to parent " + hex(parentHandle));
    return true;
}

bool NativeWindowPrivate::setRect(int32_t x, int32_t y, int32_t width, int32_t height)
{
    if (width <= 0 || height <= 0)
    {
        return false;
    }
    m_rectX = x;
    m_rectY = y;
    m_rectWidth = width;
    m_rectHeight = height;
    return applyRect();
}

bool NativeWindowPrivate::applyRect()
{
    if (!m_attached || m_viewHwnd == nullptr || ::IsWindow(m_viewHwnd) == FALSE)
    {
        return false;
    }
    if (m_parentHwnd == nullptr || ::IsWindow(m_parentHwnd) == FALSE)
    {
        return false;
    }
    // 矩形是父窗口客户区坐标；overlay 是独立弹窗，先换算成屏幕坐标
    POINT origin {m_rectX, m_rectY};
    if (::ClientToScreen(m_parentHwnd, &origin) == FALSE)
    {
        return false;
    }
    // Z 序插到 owner 正上方：直接把 owner 当插入点会被排到它下面，
    // 然后被 Chromium 的合成层盖住（WS_EX_NOREDIRECTIONBITMAP）；
    // 取 owner 前一个窗口作为插入点，等于插在 owner 正上方
    HWND insertAfter = ::GetWindow(m_parentHwnd, GW_HWNDPREV);
    UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    if (insertAfter == nullptr)
    {
        insertAfter = HWND_TOP;
    }
    else if (insertAfter == m_viewHwnd)
    {
        // 已经在 owner 正上方，不用再动 Z 序
        insertAfter = nullptr;
        flags |= SWP_NOZORDER;
    }
    return ::SetWindowPos(m_viewHwnd, insertAfter, origin.x, origin.y, m_rectWidth, m_rectHeight, flags) != FALSE;
}

bool NativeWindowPrivate::detach()
{
    if (m_viewHwnd == nullptr || ::IsWindow(m_viewHwnd) == FALSE)
    {
        return true;
    }
    ::ShowWindow(m_viewHwnd, SW_HIDE);
    if (::IsWindowVisible(m_viewHwnd))
        return false;
    m_attached = false;
    mediaservice::logger().logInfo(kComponent, "view detached");
    return true;
}

uint64_t NativeWindowPrivate::nativeHandle() const
{
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(m_viewHwnd));
}

bool NativeWindowPrivate::destroyView()
{
    if (m_viewHwnd != nullptr)
    {
        if (::IsWindow(m_viewHwnd))
        {
            if (!::DestroyWindow(m_viewHwnd))
                return false;
        }
        m_viewHwnd = nullptr;
    }
    return true;
}
