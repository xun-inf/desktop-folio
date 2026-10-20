#pragma once

#include "service.h"

#include <cstdint>
#include <map>
#include <memory>

class NativeWindow;

// 原生窗口业务：IPC Service 与窗口管理合一（原 NativeWindowMgr 收编进本类）。
// service_id = "NativeWindowService"，与 addon 侧 ServiceProxy 同名映射。
// 约定：create/refresh/setRect/setVisible/release/releaseAll 均在 UI 线程调用。
using NativeWindowId = std::uint32_t;

// 主窗口固定 id：nativeWindow()/createWindow()/releaseWindow() 直接落到主窗口。
constexpr NativeWindowId kMainNativeWindowId = 0;

class NativeWindowService : public ServiceBase<NativeWindowService>
{
public:
    NativeWindowService();
    ~NativeWindowService() override;

    NativeWindowService(const NativeWindowService&) = delete;
    NativeWindowService& operator=(const NativeWindowService&) = delete;

    const char* serviceId() const override
    {
        return "NativeWindowService";
    }

    // 统一工厂（services::registerAll 经 Service::Factory 调用；本服务无外部依赖）。
    static std::shared_ptr<Service> createService(IpcServer* server, EventLoop* eventLoop);

    // 应用生命周期线程调用：关闭全部原生窗口并停止组件操作（可重复调用）。
    void closeAll() override;

private:
    // --- 业务方法（invoke 分发由 ServiceBase 完成）---
    bool create(const services::CreateArgs& args, ReplyWriter& reply);
    bool refresh(const services::RefreshArgs& args, ReplyWriter& reply);
    bool setRect(const services::SetRectArgs& args, ReplyWriter& reply);
    bool setVisible(const services::SetVisibleArgs& args, ReplyWriter& reply);
    bool release(const services::ReleaseArgs& args, ReplyWriter& reply);
    bool releaseAll(const services::ReleaseAllArgs& args, ReplyWriter& reply);

    // --- 窗口管理（原 NativeWindowMgr 逻辑）---
    struct State
    {
        std::uint64_t host = 0;
        bool hostVisible = false;
        bool visible = false;
        bool attached = false;
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t width = 0;
        std::int32_t height = 0;
    };

    NativeWindow* findNativeWindow(NativeWindowId id);
    bool closeNativeWindow(NativeWindowId id);
    void closeNativeWindows();
    NativeWindow* createWindow(NativeWindowId id);
    NativeWindow* nativeWindow(NativeWindowId id);
    void closeWindow(NativeWindowId id);
    bool releaseWindow(NativeWindowId id);

    bool m_initialized = false;
    std::unique_ptr<NativeWindow> m_mainNativeWindow;
    std::map<NativeWindowId, std::unique_ptr<NativeWindow>> m_nativeWindows;
    std::map<NativeWindowId, State> m_states;
};
