#  SPDX-License-Identifier: Unlicense

cmake_minimum_required(VERSION 3.30)
project(multifan LANGUAGES CXX VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)
set(BUILD_SHARED_LIBS NO)

add_subdirectory(spdlog)

add_executable(multifan main.cpp)
if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    target_compile_definitions(multifan PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
endif()
target_compile_options(multifan PRIVATE -Wall -Wextra)
target_compile_definitions(multifan PRIVATE _POSIX_C_SOURCE=199309L)
target_link_libraries(multifan PRIVATE spdlog)
