#include "service_registry.h"

#include "base/logger.h"

#include <utility>

namespace
{

const char kComponent[] = "registry";

} // namespace

ServiceRegistry::ServiceRegistry() = default;

ServiceRegistry::~ServiceRegistry() = default;

bool ServiceRegistry::registerService(std::shared_ptr<Service> service)
{
    if (!service)
    {
        return false;
    }
    const std::string serviceId = service->serviceId();
    if (serviceId.empty())
    {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_services.find(serviceId) != m_services.end())
        {
            return false;
        }
        m_services[serviceId] = std::move(service);
    }
    mediaservice::logger().logInfo(kComponent, "registered service: " + serviceId);
    return true;
}

std::shared_ptr<Service> ServiceRegistry::resolve(const std::string& serviceId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_services.find(serviceId);
    return it == m_services.end() ? std::shared_ptr<Service>() : it->second;
}

void ServiceRegistry::closeAll() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& entry : m_services)
    {
        entry.second->closeAll();
    }
}

bool ServiceRegistry::dispatch(const char* serviceId, const char* method, const std::uint8_t* args,
                               std::size_t argsSize, Service::ReplyWriter& reply)
{
    if (serviceId == nullptr || method == nullptr)
    {
        return false;
    }
    const std::shared_ptr<Service> service = resolve(serviceId);
    if (!service)
    {
        mediaservice::logger().logWarn(kComponent, "unknown service: " + std::string(serviceId));
        return false;
    }
    return service->invoke(method, args, argsSize, reply);
}
