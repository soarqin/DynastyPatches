cmake_minimum_required(VERSION 3.20)
project(CastleRuntime LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(CastleRuntime SHARED
    runtime_dll.c
    CastleRuntime.def
)

target_compile_definitions(CastleRuntime PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(CastleRuntime PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(CastleRuntime PRIVATE -Wall -Wextra -Wpedantic)
endif()
