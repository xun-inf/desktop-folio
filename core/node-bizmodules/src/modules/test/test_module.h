#pragma once
#include "../../runtime/module.h"

namespace node_bizmodules
{
// Framework fixture only; it owns no account, room, media or legacy settings.
class TestModule : public Module
{
public:
    explicit TestModule(napi_env env);
    const char* moduleId() const override;
    napi_value createObject(napi_value root, const std::shared_ptr<ModuleHost>& host) override;
    napi_value echo(napi_value value) const;
    napi_value publish(napi_value value);
    uint32_t subscribeChanged(const std::string& event, napi_value callback);

private:
    bool acceptsEvent(const std::string& event) const override;
};
} // namespace node_bizmodules
