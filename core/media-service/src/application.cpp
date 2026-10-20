#include "application.h"

#include "base/logger.h"
#include "services/register.h"

#include <cstdio>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace
{

const char kComponent[] = "app";

} // namespace

MediaServiceApplication* MediaServiceApplication::s_instance = nullptr;

MediaServiceApplication::MediaServiceApplication(int argc, char** argv)
    : m_config(argc, argv), m_ipcServer([this] { m_eventLoop.requestQuit(); })
{
    mediaservice::logger().setLogFile(m_config.logPath());
    MediaServiceApplication::s_instance = this;
}

MediaServiceApplication::~MediaServiceApplication()
{
    cleanup();
    if (s_instance == this)
    {
        s_instance = nullptr;
    }
}

bool MediaServiceApplication::initialize()
{
    if (m_config.pipeName().empty())
    {
        mediaservice::logger().logError(kComponent, "missing --pipe-name=<name>");
        return false;
    }
    if (m_initialized)
    {
        return true;
    }
    if (!m_eventLoop.initialize())
    {
        mediaservice::logger().logError(kComponent, "failed to initialize event loop");
        return false;
    }

    // Service 管理层：统一注册接口（services::registerAll）构造并注册全部业务 Service。
    m_serviceRegistry.reset(new ServiceRegistry());
    if (!services::registerAll(*m_serviceRegistry, &m_ipcServer, &m_eventLoop))
    {
        mediaservice::logger().logError(kComponent, "failed to register services");
        return false;
    }

    // 路由：InvokeReq -> ServiceRegistry；UI 线程任务队列 -> EventLoop。
    m_ipcServer.setRegistry(m_serviceRegistry.get());
    m_ipcServer.setEventLoop(&m_eventLoop);

    m_initialized = true;
    return true;
}

bool MediaServiceApplication::exec()
{
    if (!m_initialized)
    {
        mediaservice::logger().logError(kComponent, "media service not initialized");
        return false;
    }

    const std::string pipeName = m_config.pipeName();
    if (!m_ipcServer.start(pipeName))
    {
        mediaservice::logger().logError(kComponent, "failed to start pipe server: " + pipeName);
        return false;
    }
    // stdout 只用于启动就绪通知；日志继续写入 stderr 和日志文件。
    std::fputs("READY\n", stdout);
    std::fflush(stdout);
    mediaservice::logger().logInfo(kComponent, "media service running");

    const bool succeeded = m_eventLoop.run();
    cleanup();
    mediaservice::logger().logInfo(kComponent, "media service stopped");
    return succeeded;
}

void MediaServiceApplication::cleanup()
{
    // 先停 IPC（等待接收线程退出），再由注册表统一收口各 Service 生命周期，最后停事件循环。
    m_ipcServer.stop();
    if (m_serviceRegistry != nullptr)
    {
        m_serviceRegistry->closeAll();
    }
    m_eventLoop.stop();
    m_serviceRegistry.reset();
    m_initialized = false;
}
