cmake_minimum_required(VERSION 3.20)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/../../../cmake")
include(CustomCompilerOptions)
include(CastlePatchesVersion)

project(CastleRuntime VERSION ${CASTLEPATCHES_VERSION} LANGUAGES C)

set(CASTLEPATCHES_FILE_TYPE VFT_DLL)
set(CASTLEPATCHES_FILE_DESCRIPTION "Castle Patches Runtime")
set(CASTLEPATCHES_INTERNAL_NAME "CastleRuntime")
set(CASTLEPATCHES_ORIGINAL_FILENAME "CastleRuntime.dll")
configure_file(../CastlePatchesVersion.rc.in CastleRuntimeVersion.rc @ONLY)

add_library(CastleRuntime SHARED
    runtime_dll.c
    CastleRuntime.def
    "${CMAKE_CURRENT_BINARY_DIR}/CastleRuntimeVersion.rc"
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
