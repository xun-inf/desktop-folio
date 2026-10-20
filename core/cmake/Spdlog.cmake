include_guard(GLOBAL)

include(FetchContent)

set(FOLIO_SPDLOG_VERSION "1.17.0" CACHE STRING
  "spdlog version used by the native components")
set(FOLIO_SPDLOG_ROOT "" CACHE PATH
  "Optional local spdlog source root for offline builds")

set(_FOLIO_SPDLOG_SOURCE_SHA256
  "d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744")

function(folio_add_spdlog)
  if(TARGET spdlog::spdlog)
    return()
  endif()

  set(SPDLOG_USE_STD_FORMAT OFF CACHE BOOL "" FORCE)
  set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
  set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "" FORCE)
  set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(SPDLOG_BUILD_TESTS_HO OFF CACHE BOOL "" FORCE)
  set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
  set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
  set(SPDLOG_SYSTEM_INCLUDES ON CACHE BOOL "" FORCE)
  # Windows 文件 API 在依赖内部使用 UTF-16；公共 Logger 接口始终接收 UTF-8。
  set(SPDLOG_WCHAR_FILENAMES "${WIN32}" CACHE BOOL "" FORCE)
  # 调试器输出也在依赖内部从 UTF-8 转换为 UTF-16。
  set(SPDLOG_WCHAR_SUPPORT "${WIN32}" CACHE BOOL "" FORCE)

  if(FOLIO_SPDLOG_ROOT)
    get_filename_component(_spdlog_root
      "${FOLIO_SPDLOG_ROOT}" ABSOLUTE BASE_DIR "${FOLIO_ROOT}")
    if(NOT EXISTS "${_spdlog_root}/CMakeLists.txt")
      message(FATAL_ERROR
        "spdlog source not found: ${_spdlog_root}. "
        "FOLIO_SPDLOG_ROOT must point to a spdlog source tree.")
    endif()
    add_subdirectory("${_spdlog_root}"
      "${CMAKE_BINARY_DIR}/_deps/folio_spdlog-build" EXCLUDE_FROM_ALL)
  else()
    FetchContent_Declare(folio_spdlog
      URL
        "https://github.com/gabime/spdlog/archive/refs/tags/v${FOLIO_SPDLOG_VERSION}.tar.gz"
      URL_HASH "SHA256=${_FOLIO_SPDLOG_SOURCE_SHA256}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(folio_spdlog)
  endif()

  if(NOT TARGET spdlog::spdlog)
    message(FATAL_ERROR "spdlog::spdlog target was not created")
  endif()
endfunction()
