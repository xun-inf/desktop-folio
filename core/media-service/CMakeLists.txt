# media-service: subprocess started by Electron; receives commands over a named pipe and maintains visible components.
add_executable(mediaService
  src/application.cpp
  src/base/config.cpp
  src/base/ipc_server.cpp
  src/base/event_loop.cpp
  src/base/logger.cpp
  src/base/service_registry.cpp
  src/main.cpp
  src/services/native_window/native_window.cpp
  src/services/native_window/native_window_service.cpp
  src/services/stream_preview/framebuffer.cpp
  src/services/stream_preview/stream_preview.cpp
  src/services/stream_preview/stream_preview_service.cpp
  src/services/session/session_service.cpp
  src/services/register.cpp
  # PE resource: icon (config/app.ico)
  resources/app.rc)

if(WIN32)
  # Windows implementations of each module; regenerate automatically when files are added or removed.
  file(GLOB_RECURSE media_service_win_sources CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
  list(FILTER media_service_win_sources INCLUDE REGEX "/platform/win/")
  target_sources(mediaService PRIVATE ${media_service_win_sources})
  target_include_directories(mediaService PRIVATE "${FOLIO_ROOT}/..")
  target_compile_definitions(mediaService PRIVATE YYMOBILE_WIN32=1)
endif()

set_target_properties(mediaService PROPERTIES OUTPUT_NAME "media-service")

target_include_directories(mediaService PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  # Cross-directory internal headers are included from the src root, e.g. "base/ipcserver_p.h".
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  # 业务子目录允许裸 include（native_window_p.h / stream_preview_p.h / framebuffer.h / gpudevice.h）。
  ${CMAKE_CURRENT_SOURCE_DIR}/src/services/native_window
  ${CMAKE_CURRENT_SOURCE_DIR}/src/services/stream_preview
  # "protos/*.h" requires the core/common include root (generated headers live in core/common/protos).
  ${CMAKE_CURRENT_SOURCE_DIR}/../common)

target_link_libraries(mediaService PRIVATE
  folio_options
  FlatBuffers::Headers
  liveUtils
  user32
  gdi32
  d3d11
  d3dcompiler
  dxgi)
