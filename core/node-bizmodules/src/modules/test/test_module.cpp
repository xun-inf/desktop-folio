#include "test_module.h"
#include "../../runtime/module_host.h"
#include <memory>

namespace node_bizmodules
{
namespace
{
enum class Operation
{
    Echo,
    Publish,
    Subscribe
};
struct MethodContext
{
    std::shared_ptr<ModuleHost> host;
    std::shared_ptr<TestModule> module;
    Operation operation;
    napi_ref owner;
    MethodContext(const std::shared_ptr<ModuleHost>& runtime, const std::shared_ptr<TestModule>& value, Operation op)
        : host(runtime), module(value), operation(op), owner(nullptr)
    {
    }
};
struct SubscriptionContext
{
    std::shared_ptr<ModuleHost> host;
    std::shared_ptr<TestModule> module;
    uint32_t id;
    SubscriptionContext(const std::shared_ptr<ModuleHost>& runtime, const std::shared_ptr<TestModule>& value,
                        uint32_t subscription)
        : host(runtime), module(value), id(subscription)
    {
    }
};
void methodFinalizer(napi_env env, void* data, void*)
{
    std::unique_ptr<MethodContext> context(static_cast<MethodContext*>(data));
    try
    {
        if (env && context->owner)
            NBM_NAPI(napi_delete_reference)(env, context->owner);
    }
    catch (...)
    {
    }
}
void subscriptionFinalizer(napi_env, void* data, void*)
{
    std::unique_ptr<SubscriptionContext> context(static_cast<SubscriptionContext*>(data));
    try
    {
        context->module->dispose(context->id);
    }
    catch (...)
    {
    }
}
napi_value disposeCallback(napi_env env, napi_callback_info info)
{
    try
    {
        void* data = nullptr;
        check(NBM_NAPI(napi_get_cb_info)(env, info, nullptr, nullptr, nullptr, &data));
        SubscriptionContext* context = static_cast<SubscriptionContext*>(data);
        context->module->dispose(context->id);
        return undefined(env);
    }
    catch (const std::exception& error)
    {
        throwError(env, error.what());
        return nullptr;
    }
    catch (...)
    {
        throwError(env, "Cannot dispose business subscription");
        return nullptr;
    }
}
napi_value subscribe(napi_env env, MethodContext* method, napi_value* args)
{
    const uint32_t id = method->module->subscribeChanged(text(env, args[0]), args[1]);
    try
    {
        std::unique_ptr<SubscriptionContext> context(new SubscriptionContext(method->host, method->module, id));
        napi_value disposer = nullptr;
        check(NBM_NAPI(napi_create_function)(env, "dispose", NAPI_AUTO_LENGTH, disposeCallback, context.get(),
                                             &disposer));
        check(NBM_NAPI(napi_add_finalizer)(env, disposer, context.get(), subscriptionFinalizer, nullptr, nullptr));
        context.release();
        napi_value owner = nullptr;
        check(NBM_NAPI(napi_get_reference_value)(env, method->owner, &owner));
        keepOwner(env, disposer, owner);
        napi_property_descriptor callback = {"_callback", nullptr, nullptr,      nullptr,
                                             nullptr,     args[1], napi_default, nullptr};
        check(NBM_NAPI(napi_define_properties)(env, disposer, 1, &callback));
        napi_value result = object(env);
        property(env, result, "dispose", disposer);
        return result;
    }
    catch (...)
    {
        method->module->dispose(id);
        throw;
    }
}
napi_value methodCallback(napi_env env, napi_callback_info info)
{
    MethodContext* context = nullptr;
    try
    {
        napi_value args[3] = {};
        size_t count = 3;
        void* data = nullptr;
        check(NBM_NAPI(napi_get_cb_info)(env, info, &count, args, nullptr, &data));
        context = static_cast<MethodContext*>(data);
        if (context->operation == Operation::Subscribe)
        {
            if (count != 2)
                throw std::runtime_error("Expected changed event and callback");
            return subscribe(env, context, args);
        }
        if (count != 1)
            throw std::runtime_error("Expected one business string argument");
        // Retain the existing type/UTF-8 byte limit validation, but preserve the
        // original JavaScript string, including unpaired UTF-16 surrogates.
        text(env, args[0]);
        return promise(env, context->operation == Operation::Echo ? context->module->echo(args[0])
                                                                  : context->module->publish(args[0]));
    }
    catch (const std::exception& error)
    {
        if (context && context->operation != Operation::Subscribe)
        {
            try
            {
                return rejectCurrent(env, error);
            }
            catch (const std::exception& rejectionError)
            {
                throwError(env, rejectionError.what());
                return nullptr;
            }
            catch (...)
            {
                throwError(env, "Cannot reject business operation");
                return nullptr;
            }
        }
        throwError(env, error.what());
        return nullptr;
    }
    catch (...)
    {
        throwError(env, "Business module callback failed");
        return nullptr;
    }
}
void addMethod(napi_env env, napi_value target, napi_value root, const std::shared_ptr<ModuleHost>& host,
               const std::shared_ptr<TestModule>& module, const char* name, Operation operation)
{
    std::unique_ptr<MethodContext> context(new MethodContext(host, module, operation));
    napi_value function = nullptr;
    check(NBM_NAPI(napi_create_function)(env, name, NAPI_AUTO_LENGTH, methodCallback, context.get(), &function));
    check(NBM_NAPI(napi_add_finalizer)(env, function, context.get(), methodFinalizer, nullptr, nullptr));
    MethodContext* wrapped = context.release();
    check(NBM_NAPI(napi_create_reference)(env, root, 0, &wrapped->owner));
    keepOwner(env, function, root);
    property(env, target, name, function);
}
} // namespace
TestModule::TestModule(napi_env env) : Module(env)
{
}
const char* TestModule::moduleId() const
{
    return "TestModule";
}
bool TestModule::acceptsEvent(const std::string& event) const
{
    return event == "changed";
}
napi_value TestModule::echo(napi_value value) const
{
    requireReady();
    return value;
}
napi_value TestModule::publish(napi_value value)
{
    requireReady();
    emit("changed", value);
    return value;
}
uint32_t TestModule::subscribeChanged(const std::string& event, napi_value callback)
{
    return subscribe(event, callback);
}
napi_value TestModule::createObject(napi_value root, const std::shared_ptr<ModuleHost>& host)
{
    napi_value result = object(environment());
    const std::shared_ptr<TestModule> module = std::static_pointer_cast<TestModule>(shared_from_this());
    addMethod(environment(), result, root, host, module, "echo", Operation::Echo);
    addMethod(environment(), result, root, host, module, "publish", Operation::Publish);
    addMethod(environment(), result, root, host, module, "subscribe", Operation::Subscribe);
    return result;
}
} // namespace node_bizmodules
