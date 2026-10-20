#include "services/native_window/native_window_service.h"

#include "services/native_window/native_window.h"

std::shared_ptr<Service> NativeWindowService::createService(IpcServer*, EventLoop*)
{
    return std::shared_ptr<Service>(new NativeWindowService());
}

NativeWindowService::NativeWindowService()
{
    this->registerMethod<services::CreateArgs>("create", &NativeWindowService::create);
    this->registerMethod<services::RefreshArgs>("refresh", &NativeWindowService::refresh);
    this->registerMethod<services::SetRectArgs>("setRect", &NativeWindowService::setRect);
    this->registerMethod<services::SetVisibleArgs>("setVisible", &NativeWindowService::setVisible);
    this->registerMethod<services::ReleaseArgs>("release", &NativeWindowService::release);
    this->registerMethod<services::ReleaseAllArgs>("releaseAll", &NativeWindowService::releaseAll);

    // 原 NativeWindowMgr::initialize()：主窗口对象先建好，实际窗口延迟到 attach 时创建。
    if (m_mainNativeWindow == nullptr)
    {
        m_mainNativeWindow.reset(new NativeWindow());
    }
    m_initialized = true;
}

NativeWindowService::~NativeWindowService()
{
    closeAll();
}

bool NativeWindowService::create(const services::CreateArgs& args, ReplyWriter& reply)
{
    const std::uint32_t id = args.id();
    const std::uint64_t host = args.host();
    const bool hostVisible = args.host_visible();

    State state;
    state.host = host;
    state.hostVisible = hostVisible;
    state.visible = hostVisible;
    state.attached = false;
    if (createWindow(id) == nullptr)
    {
        replyError(reply, "Native window manager unavailable");
        return true;
    }
    m_states[id] = state;
    if (host != 0)
    {
        NativeWindow* window = nativeWindow(id);
        if (window == nullptr || !window->attach(host, hostVisible))
        {
            replyError(reply, "Failed to attach native window");
            return true;
        }
        m_states[id].attached = true;
    }
    replyOk(reply);
    return true;
}

bool NativeWindowService::refresh(const services::RefreshArgs& args, ReplyWriter& reply)
{
    const std::uint32_t id = args.id();
    const std::uint64_t host = args.host();
    const bool hostVisible = args.host_visible();

    if (host == 0)
    {
        replyError(reply, "Refresh requires a host window handle");
        return true;
    }
    if (createWindow(id) == nullptr)
    {
        replyError(reply, "Failed to create native window");
        return true;
    }
    State state;
    state.host = host;
    state.hostVisible = hostVisible;
    state.visible = hostVisible;
    state.attached = false;
    NativeWindow* window = nativeWindow(id);
    if (window == nullptr || !window->attach(host, hostVisible))
    {
        replyError(reply, "Failed to attach native window");
        return true;
    }
    state.attached = true;
    m_states[id] = state;
    replyOk(reply);
    return true;
}

bool NativeWindowService::setRect(const services::SetRectArgs& args, ReplyWriter& reply)
{
    const std::uint32_t id = args.id();
    const std::int32_t x = args.x();
    const std::int32_t y = args.y();
    const std::int32_t width = args.width();
    const std::int32_t height = args.height();

    if (width <= 0 || height <= 0)
    {
        replyError(reply, "Invalid native window size");
        return true;
    }
    NativeWindow* window = nativeWindow(id);
    if (window == nullptr || !window->setRect(x, y, width, height))
    {
        replyError(reply, "Failed to set native window rect");
        return true;
    }
    State& state = m_states[id];
    state.x = x;
    state.y = y;
    state.width = width;
    state.height = height;
    replyOk(reply);
    return true;
}

bool NativeWindowService::setVisible(const services::SetVisibleArgs& args, ReplyWriter& reply)
{
    const std::uint32_t id = args.id();
    const bool visible = args.visible();

    const auto it = m_states.find(id);
    if (it == m_states.end())
    {
        replyError(reply, "Unknown native window id");
        return true;
    }
    it->second.visible = visible;
    if (it->second.attached)
    {
        NativeWindow* window = nativeWindow(id);
        if (window == nullptr || !window->attach(it->second.host, visible))
        {
            replyError(reply, "Failed to update native window visibility");
            return true;
        }
    }
    replyOk(reply);
    return true;
}

bool NativeWindowService::release(const services::ReleaseArgs& args, ReplyWriter& reply)
{
    const std::uint32_t id = args.id();

    m_states.erase(id);
    releaseWindow(id);
    replyOk(reply);
    return true;
}

bool NativeWindowService::releaseAll(const services::ReleaseAllArgs& args, ReplyWriter& reply)
{
    (void)args;

    m_states.clear();
    closeAll();
    replyOk(reply);
    return true;
}

// --- 窗口管理（原 NativeWindowMgr）---
NativeWindow* NativeWindowService::findNativeWindow(NativeWindowId id)
{
    if (id == kMainNativeWindowId)
    {
        return m_mainNativeWindow.get();
    }
    const auto it = m_nativeWindows.find(id);
    return it != m_nativeWindows.end() ? it->second.get() : nullptr;
}

bool NativeWindowService::closeNativeWindow(NativeWindowId id)
{
    NativeWindow* target = findNativeWindow(id);
    if (target != nullptr)
    {
        return target->close();
    }
    return true;
}

void NativeWindowService::closeNativeWindows()
{
    if (m_mainNativeWindow != nullptr)
    {
        m_mainNativeWindow->close();
    }
    for (auto& entry : m_nativeWindows)
    {
        entry.second->close();
    }
}

NativeWindow* NativeWindowService::createWindow(NativeWindowId id)
{
    if (!m_initialized)
    {
        return nullptr;
    }
    if (id == kMainNativeWindowId)
    {
        return m_mainNativeWindow.get();
    }
    auto it = m_nativeWindows.find(id);
    if (it == m_nativeWindows.end())
    {
        it = m_nativeWindows.emplace(id, std::unique_ptr<NativeWindow>(new NativeWindow())).first;
    }
    return it->second.get();
}

NativeWindow* NativeWindowService::nativeWindow(NativeWindowId id)
{
    return m_initialized ? findNativeWindow(id) : nullptr;
}

void NativeWindowService::closeWindow(NativeWindowId id)
{
    closeNativeWindow(id);
}

bool NativeWindowService::releaseWindow(NativeWindowId id)
{
    if (id == kMainNativeWindowId)
    {
        return closeNativeWindow(id);
    }
    if (!closeNativeWindow(id))
        return false;
    m_nativeWindows.erase(id);
    return true;
}

void NativeWindowService::closeAll()
{
    m_initialized = false;
    closeNativeWindows();
}
