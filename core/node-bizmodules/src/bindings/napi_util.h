#pragma once

#include <node_api.h>
#include <windows.h>
#include <stdexcept>
#include <string>

namespace node_bizmodules
{
FARPROC resolveHostApi(const char* name);
void validateHostApi();
void throwError(napi_env env, const char* message) noexcept;
#define NBM_NAPI(name) reinterpret_cast<decltype(&name)>(node_bizmodules::resolveHostApi(#name))

void check(napi_status status);
napi_value undefined(napi_env env);
napi_value object(napi_env env);
napi_value string(napi_env env, const std::string& value);
void property(napi_env env, napi_value target, const char* name, napi_value value);
std::string text(napi_env env, napi_value value);
napi_value promise(napi_env env, napi_value value, bool reject = false);
napi_value rejected(napi_env env, const std::string& message);
napi_value rejectCurrent(napi_env env, const std::exception& error);
void keepOwner(napi_env env, napi_value value, napi_value owner);
} // namespace node_bizmodules
