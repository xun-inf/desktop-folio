#include "module_host.h"
#include "module_registry.h"
#include <exception>

namespace node_bizmodules
{
ModuleHost::ModuleHost(napi_env env) : m_env(env), m_state(State::Created)
{
    createAllModules(env, m_modules);
}
void ModuleHost::install(napi_value root, const std::shared_ptr<ModuleHost>& owner)
{
    installAllModules(m_env, root, owner, m_modules);
}
void ModuleHost::initialize()
{
    if (!m_env || m_state == State::Closed)
        throw std::runtime_error("Business host is closed");
    if (m_state == State::Ready)
        return;
    try
    {
        for (size_t index = 0; index < m_modules.size(); ++index)
            m_modules[index]->initialize();
        m_state = State::Ready;
    }
    catch (...)
    {
        shutdown();
        throw;
    }
}
void ModuleHost::shutdown()
{
    if (m_state == State::Closed)
        return;
    m_state = State::Closed;
    std::exception_ptr failure;
    for (size_t index = m_modules.size(); index > 0; --index)
    {
        try
        {
            m_modules[index - 1]->shutdown();
        }
        catch (...)
        {
            if (!failure)
                failure = std::current_exception();
        }
    }
    if (failure)
        std::rethrow_exception(failure);
}
void ModuleHost::environmentCleanup()
{
    // Cleanup all modules even when one fails; finalizers must never call Node
    // after the environment cleanup hook has retired it.
    try
    {
        shutdown();
    }
    catch (...)
    {
    }
    for (size_t index = 0; index < m_modules.size(); ++index)
    {
        try
        {
            m_modules[index]->environmentCleanup();
        }
        catch (...)
        {
        }
    }
    m_env = nullptr;
}
napi_value ModuleHost::getStatus() const
{
    if (!m_env)
        throw std::runtime_error("Business environment is closed");
    napi_value result = object(m_env);
    property(m_env, result, "state",
             string(m_env, m_state == State::Created ? "created"
                           : m_state == State::Ready ? "ready"
                                                     : "closed"));
    napi_value approved = nullptr;
    check(NBM_NAPI(napi_create_array_with_length)(m_env, m_modules.size(), &approved));
    size_t subscriptions = 0;
    for (size_t index = 0; index < m_modules.size(); ++index)
    {
        check(NBM_NAPI(napi_set_element)(m_env, approved, static_cast<uint32_t>(index),
                                         string(m_env, m_modules[index]->moduleId())));
        subscriptions += m_modules[index]->subscriptionCount();
    }
    property(m_env, result, "approvedModules", approved);
    napi_value count = nullptr;
    check(NBM_NAPI(napi_create_uint32)(m_env, static_cast<uint32_t>(subscriptions), &count));
    property(m_env, result, "subscriptions", count);
    return result;
}
} // namespace node_bizmodules
