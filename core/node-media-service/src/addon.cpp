#include "runtime/service_host.h"
#include "runtime/napi_util.h"
#include "texture-handle/texture_handle.h"

#include <node_api.h>
#include <windows.h>

#include <exception>
#include <string>
#include <vector>

namespace node_media_service
{
namespace
{

std::wstring utf8ToWide(const std::string& value)
{
    if (value.empty())
    {
        return std::wstring();
    }
    const int count = ::MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &result[0], count);
    return result;
}

// 环境销毁时兜底关闭子进程；host 对象归 root finalizer 删除。
void environmentCleanup(void* data)
{
    ServiceHost* host = static_cast<ServiceHost*>(data);
    if (host != nullptr)
    {
        host->shutdown();
    }
}

ServiceHostOptions parseOptions(napi_env env, napi_value options)
{
    ServiceHostOptions result;
    const std::string executable = text(env, get(env, options, "executable"));
    if (executable.empty())
    {
        throw std::runtime_error("Missing media service executable");
    }
    result.executable = utf8ToWide(executable);
    bool has = false;
    NMS_NAPI(napi_has_named_property)(env, options, "pipeName", &has);
    if (has)
    {
        result.pipePath = utf8ToWide(text(env, get(env, options, "pipeName")));
    }
    NMS_NAPI(napi_has_named_property)(env, options, "logPath", &has);
    if (has)
    {
        result.logPath = utf8ToWide(text(env, get(env, options, "logPath")));
    }
    NMS_NAPI(napi_has_named_property)(env, options, "startupTimeoutMs", &has);
    if (has)
    {
        result.startupTimeoutMs = idValue(env, get(env, options, "startupTimeoutMs"));
    }
    NMS_NAPI(napi_has_named_property)(env, options, "shutdownTimeoutMs", &has);
    if (has)
    {
        result.shutdownTimeoutMs = idValue(env, get(env, options, "shutdownTimeoutMs"));
    }
    NMS_NAPI(napi_has_named_property)(env, options, "arguments", &has);
    if (has)
    {
        napi_value list = get(env, options, "arguments");
        uint32_t length = 0;
        if (NMS_NAPI(napi_get_array_length)(env, list, &length) != napi_ok || length > 32)
        {
            throw std::runtime_error("Invalid media service arguments");
        }
        for (uint32_t index = 0; index < length; ++index)
        {
            napi_value value = nullptr;
            NMS_NAPI(napi_get_element)(env, list, index, &value);
            result.arguments.push_back(utf8ToWide(text(env, value)));
        }
    }
    if (result.pipePath.empty())
    {
        result.pipePath = L"\\\\.\\pipe\\node-media-service-" + std::to_wstring(::GetCurrentProcessId());
    }
    if (result.startupTimeoutMs < 100 || result.startupTimeoutMs > 60000)
    {
        throw std::runtime_error("Invalid startup timeout");
    }
    if (result.shutdownTimeoutMs < 100 || result.shutdownTimeoutMs > 15000)
    {
        throw std::runtime_error("Invalid shutdown timeout");
    }
    return result;
}

} // namespace

napi_value createMediaService(napi_env env, napi_callback_info info)
{
    try
    {
        validateHostApi();
        napi_value options = nullptr;
        std::size_t count = 1;
        NMS_NAPI(napi_get_cb_info)(env, info, &count, &options, nullptr, nullptr);
        if (count < 1)
        {
            throw std::runtime_error("Missing media service options");
        }
        const ServiceHostOptions parsed = parseOptions(env, options);
        ServiceHost* host = new ServiceHost(env);
        if (!host->start(parsed))
        {
            delete host;
            throw std::runtime_error("Failed to start media service");
        }
        NMS_NAPI(napi_add_env_cleanup_hook)(env, environmentCleanup, host);
        return createServiceRoot(env, host);
    }
    catch (const std::exception& error)
    {
        NMS_NAPI(napi_throw_error)(env, nullptr, error.what());
        return nullptr;
    }
}

} // namespace node_media_service

NAPI_MODULE_INIT()
{
    try
    {
        napi_value createFunction = nullptr;
        NMS_NAPI(napi_create_function)(env, "createMediaService", NAPI_AUTO_LENGTH,
                                       node_media_service::createMediaService, nullptr, &createFunction);
        NMS_NAPI(napi_set_named_property)(env, exports, "createMediaService", createFunction);
        node_media_service::registerTextureHandleExports(env, exports);
    }
    catch (...)
    {
    }
    return exports;
}
