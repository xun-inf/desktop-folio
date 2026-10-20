#include "module_registry.h"
#include "../modules/test/test_module.h"
#include <set>

namespace node_bizmodules
{
namespace
{
std::shared_ptr<Module> createTest(napi_env env)
{
    return std::shared_ptr<Module>(new TestModule(env));
}
// This is the only native assembly point. No dynamic module lookup or business
// method string dispatcher is exposed by the addon root.
const ModuleFactory kFactories[] = {{"TestModule", "testModule", createTest}};
} // namespace
const ModuleFactory* moduleFactories()
{
    return kFactories;
}
size_t moduleFactoryCount()
{
    return sizeof(kFactories) / sizeof(kFactories[0]);
}
void createAllModules(napi_env env, std::vector<std::shared_ptr<Module>>& modules)
{
    std::set<std::string> ids;
    std::set<std::string> properties;
    for (size_t index = 0; index < moduleFactoryCount(); ++index)
    {
        const ModuleFactory& factory = kFactories[index];
        if (!ids.insert(factory.moduleId).second || !properties.insert(factory.propertyName).second)
            throw std::runtime_error("Duplicate business module registration");
        std::shared_ptr<Module> module = factory.create(env);
        if (!module || module->moduleId() != std::string(factory.moduleId))
            throw std::runtime_error("Business module registration mismatch");
        modules.push_back(module);
    }
}
void installAllModules(napi_env env, napi_value root, const std::shared_ptr<ModuleHost>& host,
                       const std::vector<std::shared_ptr<Module>>& modules)
{
    if (modules.size() != moduleFactoryCount())
        throw std::runtime_error("Business module registry mismatch");
    for (size_t index = 0; index < modules.size(); ++index)
        property(env, root, kFactories[index].propertyName, modules[index]->createObject(root, host));
}
} // namespace node_bizmodules
