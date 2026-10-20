cmake_minimum_required(VERSION 3.25)

option(FOLIO_SCHEMA_ONLY "Configure only the schema generation target" OFF)
if(FOLIO_SCHEMA_ONLY)
  project(folio VERSION 1.0.0 LANGUAGES NONE)
else()
  project(folio VERSION 1.0.0 LANGUAGES C CXX)
endif()

get_filename_component(FOLIO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FlatBuffers)
include(Spdlog)

if(FOLIO_SCHEMA_ONLY)
  include(Schemas)
  return()
endif()

folio_add_flatbuffers_headers()

# Windows resource compile: .rc -> PE resource, used by media-service icon.
if(WIN32)
  enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

folio_add_spdlog()

# Applies only to this project own targets; does not affect FetchContent third-party projects.
add_library(folio_options INTERFACE)
target_compile_definitions(folio_options INTERFACE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if (MSVC)
  target_compile_options(folio_options INTERFACE /utf-8 /W4)
endif()

add_subdirectory(utils)
add_subdirectory(media-service)
if(WIN32)
  # Integration fixtures (scripts/biz-tests, scripts/native-tests) were removed with the refactor;
  # keep the option for re-enabling them later, but do not default to ON.
  option(FOLIO_BUILD_INTEGRATION_TESTS "Build native addon integration fixtures" OFF)
  add_subdirectory(node-bizmodules)
  add_subdirectory(node-media-service)
endif()

# Unified output directory: executables go to build/bin/<Config> at repo root
# (multi-config generators append the config name; single-config like Ninja use build/bin directly).
get_filename_component(FOLIO_OUTPUT_DIR "${FOLIO_ROOT}/build/bin" ABSOLUTE)

set_target_properties(mediaService PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${FOLIO_OUTPUT_DIR}
  PDB_OUTPUT_DIRECTORY ${FOLIO_OUTPUT_DIR})
