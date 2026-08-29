cmake_minimum_required(VERSION 3.20)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/../../../cmake")
include(CustomCompilerOptions)
include(CastlePatchesVersion)

project(CastleRuntime VERSION ${CASTLEPATCHES_VERSION} LANGUAGES C)

include(Dependencies)

if(MSVC)
    enable_language(ASM_MASM)
    set(CASTLE_RUNTIME_ASM runtime_hooks_masm.asm)
else()
    set(CMAKE_ASM_NASM_OBJECT_FORMAT win32)
    enable_language(ASM_NASM)
    set(CASTLE_RUNTIME_ASM runtime_hooks_nasm.asm)
endif()

set(CASTLEPATCHES_FILE_TYPE VFT_DLL)
set(CASTLEPATCHES_FILE_DESCRIPTION "Castle Patches Runtime")
set(CASTLEPATCHES_INTERNAL_NAME "CastleRuntime")
set(CASTLEPATCHES_ORIGINAL_FILENAME "CastleRuntime.dll")
configure_file(../CastlePatchesVersion.rc.in CastleRuntimeVersion.rc @ONLY)

add_library(CastleRuntime SHARED
    runtime_dll.c
    ${CASTLE_RUNTIME_ASM}
    CastleRuntime.def
    "${CMAKE_CURRENT_BINARY_DIR}/CastleRuntimeVersion.rc"
)

target_compile_definitions(CastleRuntime PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(CastleRuntime PRIVATE dbghelp minhook)
target_include_directories(CastleRuntime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
    target_compile_options(CastleRuntime PRIVATE
        $<$<COMPILE_LANGUAGE:C>:/W4>
        $<$<COMPILE_LANGUAGE:C>:/permissive->
        $<$<COMPILE_LANGUAGE:C>:/utf-8>
    )
else()
    target_compile_options(CastleRuntime PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-Wall>
        $<$<COMPILE_LANGUAGE:C>:-Wextra>
        $<$<COMPILE_LANGUAGE:C>:-Wpedantic>
    )
endif()
