#include "napi_util.h"
#include <vector>

namespace node_bizmodules
{
FARPROC resolveHostApi(const char* name)
{
    FARPROC function = ::GetProcAddress(::GetModuleHandleW(nullptr), name);
    if (!function)
        throw std::runtime_error(std::string("Node-API host export missing: ") + name);
    return function;
}

void validateHostApi()
{
    const char* names[] = {"napi_get_undefined",
                           "napi_create_object",
                           "napi_create_string_utf8",
                           "napi_set_named_property",
                           "napi_get_value_string_utf8",
                           "napi_create_promise",
                           "napi_resolve_deferred",
                           "napi_reject_deferred",
                           "napi_create_error",
                           "napi_is_exception_pending",
                           "napi_get_and_clear_last_exception",
                           "napi_define_properties",
                           "napi_create_function",
                           "napi_add_finalizer",
                           "napi_get_cb_info",
                           "napi_create_reference",
                           "napi_delete_reference",
                           "napi_get_reference_value",
                           "napi_call_function",
                           "napi_typeof",
                           "napi_add_env_cleanup_hook",
                           "napi_remove_env_cleanup_hook",
                           "napi_wrap",
                           "napi_create_array_with_length",
                           "napi_set_element",
                           "napi_create_uint32",
                           "napi_throw_error"};
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index)
        resolveHostApi(names[index]);
}

// Safe fallback used at every C ABI boundary, including low-memory failures.
void throwError(napi_env env, const char* message) noexcept
{
    typedef napi_status(NAPI_CDECL * ThrowError)(napi_env, const char*, const char*);
    typedef napi_status(NAPI_CDECL * IsPending)(napi_env, bool*);
    HMODULE host = ::GetModuleHandleW(nullptr);
    ThrowError function = reinterpret_cast<ThrowError>(::GetProcAddress(host, "napi_throw_error"));
    IsPending isPending = reinterpret_cast<IsPending>(::GetProcAddress(host, "napi_is_exception_pending"));
    bool pending = false;
    if (isPending)
        isPending(env, &pending);
    if (function && !pending)
        function(env, nullptr, message);
}

void check(napi_status status)
{
    if (status != napi_ok)
        throw std::runtime_error("Node-API operation failed");
}
napi_value undefined(napi_env env)
{
    napi_value result = nullptr;
    check(NBM_NAPI(napi_get_undefined)(env, &result));
    return result;
}
napi_value object(napi_env env)
{
    napi_value result = nullptr;
    check(NBM_NAPI(napi_create_object)(env, &result));
    return result;
}
napi_value string(napi_env env, const std::string& value)
{
    napi_value result = nullptr;
    check(NBM_NAPI(napi_create_string_utf8)(env, value.c_str(), value.size(), &result));
    return result;
}
void property(napi_env env, napi_value target, const char* name, napi_value value)
{
    check(NBM_NAPI(napi_set_named_property)(env, target, name, value));
}
std::string text(napi_env env, napi_value value)
{
    size_t length = 0;
    if (NBM_NAPI(napi_get_value_string_utf8)(env, value, nullptr, 0, &length) != napi_ok || length > 32768)
        throw std::runtime_error("Expected string of at most 32768 UTF-8 bytes");
    std::vector<char> buffer(length + 1);
    check(NBM_NAPI(napi_get_value_string_utf8)(env, value, buffer.data(), buffer.size(), &length));
    return std::string(buffer.data(), length);
}
napi_value promise(napi_env env, napi_value value, bool reject)
{
    napi_deferred deferred = nullptr;
    napi_value result = nullptr;
    check(NBM_NAPI(napi_create_promise)(env, &deferred, &result));
    if (reject)
        check(NBM_NAPI(napi_reject_deferred)(env, deferred, value));
    else
        check(NBM_NAPI(napi_resolve_deferred)(env, deferred, value));
    return result;
}
napi_value rejected(napi_env env, const std::string& message)
{
    napi_value error = nullptr;
    check(NBM_NAPI(napi_create_error)(env, nullptr, string(env, message), &error));
    return promise(env, error, true);
}
napi_value rejectCurrent(napi_env env, const std::exception& error)
{
    bool pending = false;
    check(NBM_NAPI(napi_is_exception_pending)(env, &pending));
    if (!pending)
        return rejected(env, error.what());
    napi_value exception = nullptr;
    check(NBM_NAPI(napi_get_and_clear_last_exception)(env, &exception));
    return promise(env, exception, true);
}
void keepOwner(napi_env env, napi_value value, napi_value owner)
{
    napi_property_descriptor descriptor = {"_owner", nullptr, nullptr, nullptr, nullptr, owner, napi_default, nullptr};
    check(NBM_NAPI(napi_define_properties)(env, value, 1, &descriptor));
}
} // namespace node_bizmodules
