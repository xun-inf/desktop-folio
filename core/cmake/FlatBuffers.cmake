include_guard(GLOBAL)

include(FetchContent)

set(FOLIO_FLATBUFFERS_VERSION "25.9.23" CACHE STRING
  "FlatBuffers version shared by C++, flatc and Electron")
set(FOLIO_FLATBUFFERS_ROOT "" CACHE PATH
  "Optional FlatBuffers source root containing include/flatbuffers")
set(FOLIO_FLATC "" CACHE FILEPATH
  "Optional flatc executable; otherwise CMake downloads the pinned Windows binary")

set(_FOLIO_FLATBUFFERS_SOURCE_SHA256
  "9102253214dea6ae10c2ac966ea1ed2155d22202390b532d1dea64935c518ada")
set(_FOLIO_FLATC_WINDOWS_SHA256
  "3d6383193ecd274f5de544a6e03464a87f581befb9fc1dda9cf508fa3cce3127")

file(READ "${FOLIO_ROOT}/frontend/package.json" _folio_package_json)
string(JSON _folio_npm_flatbuffers_version
  GET "${_folio_package_json}" dependencies flatbuffers)
if(NOT _folio_npm_flatbuffers_version STREQUAL FOLIO_FLATBUFFERS_VERSION)
  message(FATAL_ERROR
    "FlatBuffers version mismatch: CMake=${FOLIO_FLATBUFFERS_VERSION}, "
    "frontend/package.json=${_folio_npm_flatbuffers_version}")
endif()

function(folio_add_flatbuffers_headers)
  if(TARGET FlatBuffers::Headers)
    return()
  endif()

  if(FOLIO_FLATBUFFERS_ROOT)
    get_filename_component(_flatbuffers_root
      "${FOLIO_FLATBUFFERS_ROOT}" ABSOLUTE BASE_DIR "${FOLIO_ROOT}")
  else()
    FetchContent_Declare(folio_fb
      URL
        "https://github.com/google/flatbuffers/archive/refs/tags/v${FOLIO_FLATBUFFERS_VERSION}.tar.gz"
      URL_HASH "SHA256=${_FOLIO_FLATBUFFERS_SOURCE_SHA256}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      SOURCE_SUBDIR _folio_no_cmake_project)
    FetchContent_MakeAvailable(folio_fb)
    set(_flatbuffers_root "${folio_fb_SOURCE_DIR}")
  endif()

  set(_flatbuffers_header "${_flatbuffers_root}/include/flatbuffers/flatbuffers.h")
  if(NOT EXISTS "${_flatbuffers_header}")
    message(FATAL_ERROR
      "FlatBuffers headers not found: ${_flatbuffers_header}. "
      "Set FOLIO_FLATBUFFERS_ROOT to a matching source tree if building offline.")
  endif()

  set(_flatbuffers_base_header "${_flatbuffers_root}/include/flatbuffers/base.h")
  file(READ "${_flatbuffers_base_header}" _flatbuffers_base)
  string(REPLACE "." ";" _flatbuffers_version_parts
    "${FOLIO_FLATBUFFERS_VERSION}")
  list(GET _flatbuffers_version_parts 0 _flatbuffers_version_major)
  list(GET _flatbuffers_version_parts 1 _flatbuffers_version_minor)
  list(GET _flatbuffers_version_parts 2 _flatbuffers_version_revision)
  foreach(_version_part MAJOR MINOR REVISION)
    string(TOLOWER "${_version_part}" _version_part_lower)
    string(FIND "${_flatbuffers_base}"
      "#define FLATBUFFERS_VERSION_${_version_part} ${_flatbuffers_version_${_version_part_lower}}"
      _version_match)
    if(_version_match EQUAL -1)
      message(FATAL_ERROR
        "FlatBuffers headers in ${_flatbuffers_root} do not match "
        "version ${FOLIO_FLATBUFFERS_VERSION}")
    endif()
  endforeach()

  add_library(folio_flatbuffers_headers INTERFACE)
  add_library(FlatBuffers::Headers ALIAS folio_flatbuffers_headers)
  target_include_directories(folio_flatbuffers_headers SYSTEM INTERFACE
    "${_flatbuffers_root}/include")
endfunction()

function(folio_add_flatc)
  if(TARGET FlatBuffers::flatc)
    return()
  endif()

  if(FOLIO_FLATC)
    get_filename_component(_flatc
      "${FOLIO_FLATC}" ABSOLUTE BASE_DIR "${FOLIO_ROOT}")
  elseif(WIN32)
    FetchContent_Declare(folio_flatc
      URL
        "https://github.com/google/flatbuffers/releases/download/v${FOLIO_FLATBUFFERS_VERSION}/Windows.flatc.binary.zip"
      URL_HASH "SHA256=${_FOLIO_FLATC_WINDOWS_SHA256}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      SOURCE_SUBDIR _folio_no_cmake_project)
    FetchContent_MakeAvailable(folio_flatc)
    set(_flatc "${folio_flatc_SOURCE_DIR}/flatc.exe")
  else()
    find_program(_flatc NAMES flatc)
  endif()

  if(NOT _flatc OR NOT EXISTS "${_flatc}")
    message(FATAL_ERROR
      "flatc not found. Set FOLIO_FLATC to a ${FOLIO_FLATBUFFERS_VERSION} executable.")
  endif()

  execute_process(
    COMMAND "${_flatc}" --version
    RESULT_VARIABLE _flatc_version_result
    OUTPUT_VARIABLE _flatc_version_output
    ERROR_VARIABLE _flatc_version_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  string(FIND "${_flatc_version_output}${_flatc_version_error}"
    "${FOLIO_FLATBUFFERS_VERSION}" _flatc_version_match)
  if(NOT _flatc_version_result EQUAL 0 OR _flatc_version_match EQUAL -1)
    message(FATAL_ERROR
      "flatc ${_flatc} does not report version ${FOLIO_FLATBUFFERS_VERSION}")
  endif()

  add_executable(folio_flatc IMPORTED GLOBAL)
  set_target_properties(folio_flatc PROPERTIES IMPORTED_LOCATION "${_flatc}")
  add_executable(FlatBuffers::flatc ALIAS folio_flatc)
endfunction()
