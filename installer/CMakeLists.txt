cmake_minimum_required(VERSION 3.25)
project(FolioSetup VERSION ${SETUP_VERSION} LANGUAGES CXX RC)
if(NOT WIN32 OR NOT EXISTS "${SETUP_PAYLOAD}")
  message(FATAL_ERROR "SETUP_PAYLOAD must point to the generated Windows NSIS installer")
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
configure_file(setup.rc.in "${CMAKE_CURRENT_BINARY_DIR}/setup.rc" @ONLY)
add_executable(folioSetup WIN32 main.cpp "${CMAKE_CURRENT_BINARY_DIR}/setup.rc")
target_compile_features(folioSetup PRIVATE cxx_std_20)
target_compile_definitions(folioSetup PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
  SETUP_VERSION=L"${PROJECT_VERSION}" SETUP_GUID=L"${SETUP_GUID}"
  SETUP_REQUIRED_BYTES=${SETUP_REQUIRED_BYTES}ULL)
target_compile_options(folioSetup PRIVATE /utf-8 /W4)
target_link_libraries(folioSetup PRIVATE gdiplus comctl32 shell32 ole32 advapi32 uxtheme)
set_target_properties(folioSetup PROPERTIES
  OUTPUT_NAME "folio-${PROJECT_VERSION}-windows-x64-setup"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${SETUP_OUTPUT_DIR}")
target_link_options(folioSetup PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/setup.manifest")
