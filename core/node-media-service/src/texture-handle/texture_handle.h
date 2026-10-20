#pragma once

#include <node_api.h>

namespace node_media_service
{
// 原 node-streampreview 独立 addon 的纹理句柄工具（跨进程复制 HANDLE + 关闭），并入本 addon。
// 由 addon.cpp 的 NAPI_MODULE_INIT 调用，把 duplicate/close 挂到模块导出。
napi_value registerTextureHandleExports(napi_env env, napi_value exports);
} // namespace node_media_service