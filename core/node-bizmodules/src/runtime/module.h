#pragma once
#include "../bindings/napi_util.h"
#include <cstdint>
#include <map>
#include <memory>

namespace node_bizmodules
{
class ModuleHost;

// Each module owns its lifecycle, event contracts and subscriptions. All access
// stays on the originating JavaScript thread.
class Module : public std::enable_shared_from_this<Module>
{
public:
    explicit Module(napi_env env);
    virtual ~Module();
    virtual const char* moduleId() const = 0;
    virtual napi_value createObject(napi_value root, const std::shared_ptr<ModuleHost>& host) = 0;
    void initialize();
    void shutdown();
    void environmentCleanup();
    size_t subscriptionCount() const;
    void dispose(uint32_t id);

protected:
    void requireReady() const;
    uint32_t subscribe(const std::string& event, napi_value callback);
    void emit(const std::string& event, napi_value payload);
    virtual bool acceptsEvent(const std::string& event) const = 0;
    virtual void onInitialize();
    virtual void onShutdown();
    napi_env environment() const;

private:
    struct Subscription
    {
        std::string event;
        napi_ref callback;
        Subscription(const std::string& name, napi_ref reference) : event(name), callback(reference)
        {
        }
    };
    enum class State
    {
        Created,
        Ready,
        Closed
    };
    napi_env m_env;
    State m_state;
    uint32_t m_nextSubscription;
    std::map<uint32_t, Subscription> m_subscriptions;
};
} // namespace node_bizmodules
