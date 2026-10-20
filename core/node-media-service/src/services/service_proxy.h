#pragma once

#include <cstdint>

namespace node_media_service
{
class Transport;

// 服务映射基类：service_id = 类名，两端（media-service / node-media-service）一致。
// addon 侧只做转发，不维护业务状态；业务状态在 media-service 的服务端 Service 里。
class ServiceProxy
{
public:
    explicit ServiceProxy(Transport* transport) : m_transport(transport)
    {
    }
    virtual ~ServiceProxy() = default;

    ServiceProxy(const ServiceProxy&) = delete;
    ServiceProxy& operator=(const ServiceProxy&) = delete;

    virtual const char* serviceId() const = 0;

    Transport* transport() const
    {
        return m_transport;
    }

protected:
    Transport* m_transport;
};
} // namespace node_media_service
