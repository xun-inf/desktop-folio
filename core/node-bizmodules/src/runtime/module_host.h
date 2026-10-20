#pragma once
#include "module.h"
#include <vector>

namespace node_bizmodules
{
// Host coordinates lifecycle only. Business methods/events stay in each module.
class ModuleHost
{
public:
    explicit ModuleHost(napi_env env);
    void install(napi_value root, const std::shared_ptr<ModuleHost>& owner);
    void initialize();
    void shutdown();
    void environmentCleanup();
    napi_value getStatus() const;

private:
    napi_env m_env;
    enum class State
    {
        Created,
        Ready,
        Closed
    } m_state;
    std::vector<std::shared_ptr<Module>> m_modules;
};
} // namespace node_bizmodules
