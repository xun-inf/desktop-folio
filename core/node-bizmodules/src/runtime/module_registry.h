#pragma once
#include "module.h"
#include <vector>

namespace node_bizmodules
{
struct ModuleFactory
{
    const char* moduleId;
    const char* propertyName;
    std::shared_ptr<Module> (*create)(napi_env env);
};
const ModuleFactory* moduleFactories();
size_t moduleFactoryCount();
void createAllModules(napi_env env, std::vector<std::shared_ptr<Module>>& modules);
void installAllModules(napi_env env, napi_value root, const std::shared_ptr<ModuleHost>& host,
                       const std::vector<std::shared_ptr<Module>>& modules);
} // namespace node_bizmodules
