#include "runtime/napi_util.h"

namespace node_media_service
{
FARPROC resolveHostApi(const char* name)
{
    return ::GetProcAddress(::GetModuleHandleW(nullptr), name);
}

void validateHostApi()
{
    const char* required[] = {
        "napi_get_cb_info",
        "napi_create_function",
        "napi_create_promise",
        "napi_resolve_deferred",
        "napi_reject_deferred",
        "napi_create_threadsafe_function",
        "napi_call_threadsafe_function",
        "napi_get_value_uint32",
        "napi_get_value_int32",
        "napi_get_value_double",
        "napi_get_value_bool",
        "napi_get_value_bigint_uint64",
        "napi_get_value_bigint_int64",
        "napi_create_buffer_copy",
        "napi_create_bigint_uint64",
    };
    for (const char* name : required)
    {
        if (resolveHostApi(name) == nullptr)
        {
            throw std::runtime_error(std::string("Host does not export required N-API symbol: ") + name);
        }
    }
}

void check(napi_status status)
{
    if (status != napi_ok)
    {
        throw std::runtime_error("N-API call failed");
    }
}

napi_value undefined(napi_env env)
{
    napi_value value = nullptr;
    NMS_NAPI(napi_get_undefined)(env, &value);
    return value;
}

napi_value object(napi_env env)
{
    napi_value value = nullptr;
    NMS_NAPI(napi_create_object)(env, &value);
    return value;
}

napi_value string(napi_env env, const std::string& value)
{
    napi_value result = nullptr;
    NMS_NAPI(napi_create_string_utf8)(env, value.c_str(), value.size(), &result);
    return result;
}

napi_value number(napi_env env, double value)
{
    napi_value result = nullptr;
    NMS_NAPI(napi_create_double)(env, value, &result);
    return result;
}

napi_value boolean(napi_env env, bool value)
{
    napi_value result = nullptr;
    NMS_NAPI(napi_get_boolean)(env, value, &result);
    return result;
}

napi_value bigint(napi_env env, uint64_t value)
{
    napi_value result = nullptr;
    NMS_NAPI(napi_create_bigint_uint64)(env, value, &result);
    return result;
}

// 把子进程句柄值复制为本进程句柄的 Buffer；失败返回 undefined（方案 B：帧归还由业务方调用 releaseVideoFrame）。
napi_value handleBuffer(napi_env env, DWORD pid, uint64_t handleValue)
{
    HANDLE source = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(handleValue));
    HANDLE local = nullptr;
    if (pid != 0)
    {
        const HANDLE process = ::OpenProcess(PROCESS_DUP_HANDLE, FALSE, pid);
        if (process != nullptr)
        {
            ::DuplicateHandle(process, source, ::GetCurrentProcess(), &local, 0, FALSE, DUPLICATE_SAME_ACCESS);
            ::CloseHandle(process);
        }
    }
    if (local == nullptr)
    {
        return undefined(env);
    }
    napi_value buffer = nullptr;
    if (NMS_NAPI(napi_create_buffer_copy)(env, sizeof(HANDLE), &local, nullptr, &buffer) != napi_ok)
    {
        ::CloseHandle(local);
        return undefined(env);
    }
    return buffer;
}

napi_value exception(napi_env env, const std::string& message)
{
    napi_value result = nullptr;
    NMS_NAPI(napi_create_error)(env, nullptr, string(env, message), &result);
    return result;
}

void property(napi_env env, napi_value target, const char* name, napi_value value)
{
    NMS_NAPI(napi_set_named_property)(env, target, name, value);
}

napi_value get(napi_env env, napi_value object, const char* name)
{
    napi_value value = nullptr;
    NMS_NAPI(napi_get_named_property)(env, object, name, &value);
    return value;
}

std::string text(napi_env env, napi_value value)
{
    std::size_t length = 0;
    if (NMS_NAPI(napi_get_value_string_utf8)(env, value, nullptr, 0, &length) != napi_ok)
    {
        throw std::runtime_error("Expected a string argument");
    }
    std::string result(length, '\0');
    if (length != 0)
    {
        NMS_NAPI(napi_get_value_string_utf8)(env, value, &result[0], length + 1, &length);
        result.resize(length);
    }
    return result;
}

void keepOwner(napi_env env, napi_value value, napi_value owner)
{
    napi_property_descriptor descriptor = {"_owner", nullptr, nullptr, nullptr, nullptr, owner, napi_default, nullptr};
    NMS_NAPI(napi_define_properties)(env, value, 1, &descriptor);
}

uint32_t idValue(napi_env env, napi_value value)
{
    return uint32Value(env, value);
}

uint32_t uint32Value(napi_env env, napi_value value)
{
    uint32_t result = 0;
    if (NMS_NAPI(napi_get_value_uint32)(env, value, &result) != napi_ok)
    {
        throw std::runtime_error("Expected an unsigned 32-bit number");
    }
    return result;
}

int32_t int32Value(napi_env env, napi_value value)
{
    int32_t result = 0;
    if (NMS_NAPI(napi_get_value_int32)(env, value, &result) != napi_ok)
    {
        throw std::runtime_error("Expected a signed 32-bit number");
    }
    return result;
}

int64_t longValue(napi_env env, napi_value value)
{
    napi_valuetype type = napi_undefined;
    NMS_NAPI(napi_typeof)(env, value, &type);
    if (type == napi_bigint)
    {
        int64_t result = 0;
        bool lossless = false;
        NMS_NAPI(napi_get_value_bigint_int64)(env, value, &result, &lossless);
        if (!lossless)
        {
            throw std::runtime_error("Int64 argument out of range");
        }
        return result;
    }
    if (type == napi_number)
    {
        return static_cast<int64_t>(numberValue(env, value));
    }
    throw std::runtime_error("Expected a number or bigint");
}

uint64_t ulongValue(napi_env env, napi_value value)
{
    napi_valuetype type = napi_undefined;
    NMS_NAPI(napi_typeof)(env, value, &type);
    if (type == napi_bigint)
    {
        uint64_t result = 0;
        bool lossless = false;
        NMS_NAPI(napi_get_value_bigint_uint64)(env, value, &result, &lossless);
        if (!lossless)
        {
            throw std::runtime_error("Uint64 argument out of range");
        }
        return result;
    }
    if (type == napi_number)
    {
        return static_cast<uint64_t>(numberValue(env, value));
    }
    throw std::runtime_error("Expected a number or bigint");
}

double numberValue(napi_env env, napi_value value)
{
    double result = 0;
    if (NMS_NAPI(napi_get_value_double)(env, value, &result) != napi_ok)
    {
        throw std::runtime_error("Expected a number");
    }
    return result;
}

bool boolValue(napi_env env, napi_value value)
{
    bool result = false;
    if (NMS_NAPI(napi_get_value_bool)(env, value, &result) != napi_ok)
    {
        throw std::runtime_error("Expected a boolean");
    }
    return result;
}

napi_value resolved(napi_env env, napi_value value, const std::string& message)
{
    napi_deferred deferred = nullptr;
    napi_value promise = nullptr;
    NMS_NAPI(napi_create_promise)(env, &deferred, &promise);
    NMS_NAPI(napi_resolve_deferred)(env, deferred, value);
    return promise;
}

napi_value rejected(napi_env env, const std::string& message)
{
    napi_deferred deferred = nullptr;
    napi_value promise = nullptr;
    NMS_NAPI(napi_create_promise)(env, &deferred, &promise);
    NMS_NAPI(napi_reject_deferred)(env, deferred, exception(env, message));
    return promise;
}

napi_value rejectCurrent(napi_env env, const std::exception& error)
{
    return rejected(env, error.what());
}

napi_value stringArray(napi_env env, const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>* values)
{
    napi_value array = nullptr;
    NMS_NAPI(napi_create_array_with_length)(env, values ? values->size() : 0, &array);
    if (values)
    {
        for (std::size_t index = 0; index < values->size(); ++index)
        {
            const flatbuffers::String* item = (*values)[index];
            napi_value element = string(env, item ? item->str() : std::string());
            NMS_NAPI(napi_set_element)(env, array, static_cast<uint32_t>(index), element);
        }
    }
    return array;
}
} // namespace node_media_service
