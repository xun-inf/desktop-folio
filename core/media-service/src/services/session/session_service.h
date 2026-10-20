#pragma once

#include "service.h"

class EventLoop;

// SessionService：握手与退出（服务端内置服务，类名即 service_id）。
// hello 在 IPC 线程同步处理（连接期握手）；shutdown 回执后请求退出事件循环。
class SessionService : public ServiceBase<SessionService>
{
public:
    SessionService(IpcServer* server, EventLoop* eventLoop);

    const char* serviceId() const override
    {
        return "SessionService";
    }

    // 统一工厂（services::registerAll 经 Service::Factory 调用）。
    static std::shared_ptr<Service> createService(IpcServer* server, EventLoop* eventLoop);

private:
    bool hello(const services::HelloArgs& args, ReplyWriter& reply);
    bool shutdown(const services::ShutdownArgs& args, ReplyWriter& reply);

    EventLoop* m_eventLoop;
};
