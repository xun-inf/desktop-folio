# live-utils：日志 / ini 配置 / ScopeGuard / 字符串工具，两个 exe 都直接链它
add_library(liveUtils STATIC
  src/logger.cpp
  src/stringutil.cpp)

set_target_properties(liveUtils PROPERTIES OUTPUT_NAME "live-utils")
target_include_directories(liveUtils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(liveUtils
  PUBLIC folio_options
  PRIVATE spdlog::spdlog)
