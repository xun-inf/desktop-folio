#pragma once

#include "service.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>

// Service 注册表：service_id = 类名，按类名路由 InvokeReq；
// 事件由 ServiceBase 经 IpcServer::emitEvent 发射（见 StreamPreviewService::emitFrameEvent）。
class ServiceRegistry
{
public:
    ServiceRegistry();
    ~ServiceRegistry();

    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;

    // 注册一个 Service；serviceId 重复时返回 false。
    bool registerService(std::shared_ptr<Service> service);

    // 按 service_id 取 Service；未注册返回 nullptr。
    std::shared_ptr<Service> resolve(const std::string& serviceId) const;

    // 分发一个 InvokeReq：解析 service/method/args 并执行 invoke，
    // 结果通过 reply.write 回执；未知 service 或方法返回 false。
    bool dispatch(const char* serviceId, const char* method, const std::uint8_t* args, std::size_t argsSize,
                  Service::ReplyWriter& reply);

    // 应用退出前统一收口：依次调用已注册 Service 的 closeAll()（应用生命周期线程调用）。
    void closeAll() const;

private:
    mutable std::mutex m_mutex;
    std::map<std::string, std::shared_ptr<Service>> m_services;
};
