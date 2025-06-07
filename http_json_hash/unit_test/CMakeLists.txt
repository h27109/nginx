cmake_minimum_required(VERSION 3.10)
project(nginx_json_hash_unit_tests C)

# 编译选项
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -g")

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# CMocka依赖
include(FetchContent)
FetchContent_Declare(
  cmocka
  GIT_REPOSITORY https://git.cryptomilk.org/projects/cmocka.git
  GIT_TAG cmocka-1.1.5
)
FetchContent_MakeAvailable(cmocka)

# 设置Nginx源码目录
set(NGINX_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
set(MODULE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../http_json_hash/src/http/modules/ngx_http_upstream_json_hash_module.c)

# 设置包含目录
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${cmocka_SOURCE_DIR}/include
  ${NGINX_SRC_DIR}
  ${NGINX_SRC_DIR}/core
  ${NGINX_SRC_DIR}/http
  ${NGINX_SRC_DIR}/event
  /usr/include/cjson  # cJSON 库头文件目录
)

# 定义编译选项，使模块代码可在测试环境中编译
add_compile_definitions(
  UNIT_TEST
  NGX_TEST_BUILD
)

# 添加模块源文件 - 通过包装层适配原始代码
add_library(module_wrapper STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/module_wrapper.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nginx_globals.c
)

# 添加测试目录
add_subdirectory(tests)

# 打印CMocka信息
message(STATUS "CMocka include directory: ${cmocka_SOURCE_DIR}/include")
message(STATUS "CMocka library directory: ${cmocka_BINARY_DIR}/src")
message(STATUS "Nginx source directory: ${NGINX_SRC_DIR}")
message(STATUS "Module source file: ${MODULE_SRC}") 