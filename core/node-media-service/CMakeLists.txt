cmake_minimum_required(VERSION 3.16)

# node-media-service: media service addon (Node-API).
# Built with scripts/build.py (CMake; no node.lib link, N-API symbols resolved from the host process).

set(FOLIO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

project(node_media_service LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/W3)
else()
  add_compile_options(-Wall -Wextra)
endif()

file(GLOB _node_api_candidates "$ENV{LOCALAPPDATA}/node-gyp/Cache/*/include/node")
find_path(FOLIO_NODE_API_INCLUDE node_api.h HINTS ${_node_api_candidates})
if(NOT FOLIO_NODE_API_INCLUDE)
  message(FATAL_ERROR "Set FOLIO_NODE_API_INCLUDE to the Node.js include/node directory")
endif()

add_library(node_media_service MODULE
  src/addon.cpp
  src/texture-handle/texture_handle.cpp
  src/services/native_window_service.cpp
  src/services/stream_preview_service.cpp
  src/services/service_registry.cpp
  src/runtime/napi_util.cpp
  src/runtime/transport.cpp
  src/runtime/service_host.cpp
)

target_include_directories(node_media_service PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  # "protos/*.h" requires the core/common include root (generated headers live in core/common/protos).
  "${FOLIO_ROOT}/core/common"
  "${FOLIO_NODE_API_INCLUDE}"
)
target_compile_definitions(node_media_service PRIVATE NAPI_VERSION=8 YYMOBILE_WIN32=1 NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_features(node_media_service PRIVATE cxx_std_11)
if(TARGET folio_options)
  target_link_libraries(node_media_service PRIVATE folio_options FlatBuffers::Headers)
elseif(MSVC)
  target_compile_options(node_media_service PRIVATE /utf-8)
endif()

set_target_properties(node_media_service PROPERTIES
  PREFIX ""
  SUFFIX ".node"
  OUTPUT_NAME "node-media-service"
  LIBRARY_OUTPUT_DIRECTORY "${FOLIO_ROOT}/build/bin"
  RUNTIME_OUTPUT_DIRECTORY "${FOLIO_ROOT}/build/bin"
)
