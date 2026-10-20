#include "runtime/module_host.h"
#include <memory>

namespace node_bizmodules
{
namespace
{
struct RootContext
{
    std::shared_ptr<ModuleHost> host;
    bool cleanupHookActive;
    explicit RootContext(napi_env env) : host(new ModuleHost(env)), cleanupHookActive(false)
    {
    }
};
enum class Operation
{
    Initialize,
    Shutdown,
    Status
};
struct MethodContext
{
    std::shared_ptr<ModuleHost> host;
    Operation operation;
    MethodContext(const std::shared_ptr<ModuleHost>& runtime, Operation op) : host(runtime), operation(op)
    {
    }
};
void cleanupHook(void* data)
{
    RootContext* context = static_cast<RootContext*>(data);
    context->cleanupHookActive = false;
    try
    {
        context->host->environmentCleanup();
    }
    catch (...)
    {
    }
}
void rootFinalizer(napi_env env, void* data, void*)
{
    std::unique_ptr<RootContext> context(static_cast<RootContext*>(data));
    try
    {
        if (context->cleanupHookActive)
        {
            check(NBM_NAPI(napi_remove_env_cleanup_hook)(env, cleanupHook, context.get()));
            context->cleanupHookActive = false;
        }
        context->host->environmentCleanup();
    }
    catch (...)
    {
    }
}
void methodFinalizer(napi_env, void* data, void*)
{
    delete static_cast<MethodContext*>(data);
}
napi_value methodCallback(napi_env env, napi_callback_info info)
{
    MethodContext* context = nullptr;
    try
    {
        void* data = nullptr;
        check(NBM_NAPI(napi_get_cb_info)(env, info, nullptr, nullptr, nullptr, &data));
        context = static_cast<MethodContext*>(data);
        switch (context->operation)
        {
        case Operation::Initialize:
            context->host->initialize();
            return promise(env, undefined(env));
        case Operation::Shutdown:
            context->host->shutdown();
            return promise(env, undefined(env));
        case Operation::Status:
            return context->host->getStatus();
        }
    }
    catch (const std::exception& error)
    {
        if (context && context->operation != Operation::Status)
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
                throwError(env, "Cannot reject business lifecycle operation");
                return nullptr;
            }
        }
        throwError(env, error.what());
        return nullptr;
    }
    catch (...)
    {
        throwError(env, "Business host callback failed");
        return nullptr;
    }
    throwError(env, "Unknown business host operation");
    return nullptr;
}
void addMethod(napi_env env, napi_value root, const std::shared_ptr<ModuleHost>& host, const char* name,
               Operation operation)
{
    std::unique_ptr<MethodContext> context(new MethodContext(host, operation));
    napi_value function = nullptr;
    check(NBM_NAPI(napi_create_function)(env, name, NAPI_AUTO_LENGTH, methodCallback, context.get(), &function));
    check(NBM_NAPI(napi_add_finalizer)(env, function, context.get(), methodFinalizer, nullptr, nullptr));
    context.release();
    keepOwner(env, function, root);
    property(env, root, name, function);
}
napi_value create(napi_env env, napi_callback_info)
{
    try
    {
        std::unique_ptr<RootContext> context(new RootContext(env));
        napi_value root = object(env);
        addMethod(env, root, context->host, "initialize", Operation::Initialize);
        addMethod(env, root, context->host, "shutdown", Operation::Shutdown);
        addMethod(env, root, context->host, "getStatus", Operation::Status);
        context->host->install(root, context->host);
        check(NBM_NAPI(napi_wrap)(env, root, context.get(), rootFinalizer, nullptr, nullptr));
        RootContext* wrapped = context.release();
        check(NBM_NAPI(napi_add_env_cleanup_hook)(env, cleanupHook, wrapped));
        wrapped->cleanupHookActive = true;
        return root;
    }
    catch (const std::exception& error)
    {
        throwError(env, error.what());
        return nullptr;
    }
    catch (...)
    {
        throwError(env, "Cannot create business host");
        return nullptr;
    }
}
} // namespace
} // namespace node_bizmodules

NAPI_MODULE_INIT()
{
    try
    {
        node_bizmodules::validateHostApi();
        napi_value create = nullptr;
        node_bizmodules::check(NBM_NAPI(napi_create_function)(env, "createBizModules", NAPI_AUTO_LENGTH,
                                                              node_bizmodules::create, nullptr, &create));
        node_bizmodules::property(env, exports, "createBizModules", create);
        return exports;
    }
    catch (const std::exception& error)
    {
        node_bizmodules::throwError(env, error.what());
        return nullptr;
    }
    catch (...)
    {
        node_bizmodules::throwError(env, "Cannot initialize business addon");
        return nullptr;
    }
}
