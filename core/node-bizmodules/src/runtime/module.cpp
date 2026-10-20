#include "module.h"
#include <limits>
#include <vector>

namespace node_bizmodules
{
Module::Module(napi_env env) : m_env(env), m_state(State::Created), m_nextSubscription(1)
{
}
Module::~Module()
{
}
void Module::initialize()
{
    if (!m_env || m_state == State::Closed)
        throw std::runtime_error("Business module is closed");
    if (m_state == State::Ready)
        return;
    onInitialize();
    m_state = State::Ready;
}
void Module::shutdown()
{
    if (m_state == State::Closed)
        return;
    m_state = State::Closed;
    while (!m_subscriptions.empty())
        dispose(m_subscriptions.begin()->first);
    onShutdown();
}
void Module::environmentCleanup()
{
    shutdown();
    m_env = nullptr;
}
void Module::onInitialize()
{
}
void Module::onShutdown()
{
}
napi_env Module::environment() const
{
    return m_env;
}
size_t Module::subscriptionCount() const
{
    return m_subscriptions.size();
}
void Module::requireReady() const
{
    if (!m_env || m_state != State::Ready)
        throw std::runtime_error("Business module is not ready");
}
uint32_t Module::subscribe(const std::string& event, napi_value callback)
{
    requireReady();
    if (!acceptsEvent(event))
        throw std::runtime_error("Business event is not approved");
    napi_valuetype type = napi_undefined;
    check(NBM_NAPI(napi_typeof)(m_env, callback, &type));
    if (type != napi_function)
        throw std::runtime_error("Expected business event callback");
    if (m_nextSubscription == std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("Business subscription capacity exceeded");
    napi_ref reference = nullptr;
    // The disposer owns the callback in the JavaScript graph; no native strong
    // reference may retain an unreachable closure/root cycle.
    check(NBM_NAPI(napi_create_reference)(m_env, callback, 0, &reference));
    const uint32_t id = m_nextSubscription++;
    try
    {
        m_subscriptions.insert(std::make_pair(id, Subscription(event, reference)));
    }
    catch (...)
    {
        NBM_NAPI(napi_delete_reference)(m_env, reference);
        throw;
    }
    return id;
}
void Module::dispose(uint32_t id)
{
    std::map<uint32_t, Subscription>::iterator found = m_subscriptions.find(id);
    if (found == m_subscriptions.end())
        return;
    if (m_env)
        check(NBM_NAPI(napi_delete_reference)(m_env, found->second.callback));
    m_subscriptions.erase(found);
}
void Module::emit(const std::string& event, napi_value payload)
{
    requireReady();
    if (!acceptsEvent(event))
        throw std::runtime_error("Business event is not approved");
    std::vector<uint32_t> ids;
    for (std::map<uint32_t, Subscription>::const_iterator it = m_subscriptions.begin(); it != m_subscriptions.end();
         ++it)
        if (it->second.event == event)
            ids.push_back(it->first);
    // Re-read each ID: callbacks may dispose listeners, subscribe or shut down.
    for (size_t index = 0; index < ids.size() && m_state == State::Ready; ++index)
    {
        std::map<uint32_t, Subscription>::const_iterator found = m_subscriptions.find(ids[index]);
        if (found == m_subscriptions.end())
            continue;
        napi_value callback = nullptr;
        check(NBM_NAPI(napi_get_reference_value)(m_env, found->second.callback, &callback));
        if (!callback)
        {
            dispose(ids[index]);
            continue;
        }
        napi_value result = nullptr;
        check(NBM_NAPI(napi_call_function)(m_env, undefined(m_env), callback, 1, &payload, &result));
    }
}
} // namespace node_bizmodules
