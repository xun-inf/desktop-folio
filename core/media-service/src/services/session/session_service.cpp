#include "services/session/session_service.h"

#include "event_loop.h"
#include "ipc_server.h"

namespace
{

constexpr std::uint32_t kProtocolVersion = 1;

} // namespace

std::shared_ptr<Service> SessionService::createService(IpcServer* server, EventLoop* eventLoop)
{
    return std::shared_ptr<Service>(new SessionService(server, eventLoop));
}

SessionService::SessionService(IpcServer* server, EventLoop* eventLoop) : ServiceBase(server), m_eventLoop(eventLoop)
{
    this->registerMethod<services::HelloArgs>("hello", &SessionService::hello);
    this->registerMethod<services::ShutdownArgs>("shutdown", &SessionService::shutdown);
}

bool SessionService::hello(const services::HelloArgs& args, ReplyWriter& reply)
{
    IpcServer* server = this->server();
    const bool compatible = args.version() == kProtocolVersion && (server == nullptr || server->session() == 0);
    if (!compatible)
    {
        replyError(reply, "Incompatible protocol or duplicate handshake");
        return true;
    }
    if (server != nullptr)
    {
        server->setSession(1);
    }
    replyResult<services::HelloResult>(
        reply, [server](flatbuffers::FlatBufferBuilder& builder)
        { return services::CreateHelloResult(builder, server ? server->session() : 0, true); });
    return true;
}

bool SessionService::shutdown(const services::ShutdownArgs& args, ReplyWriter& reply)
{
    (void)args;

    replyOk(reply);
    if (m_eventLoop != nullptr)
    {
        m_eventLoop->requestQuit();
    }
    return true;
}
