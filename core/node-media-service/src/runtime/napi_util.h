#pragma once

#include <node_api.h>
#include <windows.h>
#include <flatbuffers/string.h>
#include <flatbuffers/vector.h>

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace node_media_service
{
// 不链接 node.lib：从宿主进程（electron.exe / node.exe）导出表解析 N-API 符号。
FARPROC resolveHostApi(const char* name);
void validateHostApi();

#define NMS_NAPI(name) reinterpret_cast<decltype(&name)>(node_media_service::resolveHostApi(#name))

void check(napi_status status);

napi_value undefined(napi_env env);
napi_value object(napi_env env);
napi_value string(napi_env env, const std::string& value);
napi_value number(napi_env env, double value);
napi_value bigint(napi_env env, uint64_t value);
napi_value bigint(napi_env env, uint64_t value);
napi_value boolean(napi_env env, bool value);
napi_value exception(napi_env env, const std::string& message);
void property(napi_env env, napi_value target, const char* name, napi_value value);
napi_value get(napi_env env, napi_value object, const char* name);
std::string text(napi_env env, napi_value value);
void keepOwner(napi_env env, napi_value value, napi_value owner);

uint32_t idValue(napi_env env, napi_value value);
uint32_t uint32Value(napi_env env, napi_value value);
int32_t int32Value(napi_env env, napi_value value);
int64_t longValue(napi_env env, napi_value value);
uint64_t ulongValue(napi_env env, napi_value value);
// 把子进程句柄值复制为本进程句柄的 Buffer；失败返回 undefined（方案 B：帧归还由业务方调用 releaseVideoFrame）。
napi_value handleBuffer(napi_env env, DWORD pid, uint64_t handleValue);
// 把子进程句柄值复制为本进程句柄的 Buffer；失败返回 undefined（方案 B：帧归还由业务方调用 releaseVideoFrame）。
napi_value handleBuffer(napi_env env, DWORD pid, uint64_t handleValue);
double numberValue(napi_env env, napi_value value);
bool boolValue(napi_env env, napi_value value);

napi_value resolved(napi_env env, napi_value value, const std::string& message);
napi_value rejected(napi_env env, const std::string& message);
napi_value rejectCurrent(napi_env env, const std::exception& error);

template <typename T>
napi_value numberArray(napi_env env, const flatbuffers::Vector<T>* values)
{
    napi_value array = nullptr;
    NMS_NAPI(napi_create_array_with_length)(env, values ? values->size() : 0, &array);
    if (values)
    {
        for (std::size_t index = 0; index < values->size(); ++index)
        {
            napi_value item = number(env, static_cast<double>((*values)[index]));
            NMS_NAPI(napi_set_element)(env, array, static_cast<uint32_t>(index), item);
        }
    }
    return array;
}
napi_value stringArray(napi_env env, const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>* values);
} // namespace node_media_service
