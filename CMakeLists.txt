cmake_minimum_required(VERSION 3.20)

project(CastlePatches LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(CastlePatches WIN32
    src/CastlePatches/main.c
    src/CastlePatches/CastlePatches.rc
)

if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
    # RPG.exe is a 32-bit process.  Build the injected runtime DLL with a
    # separate Win32 project even when the launcher itself is x64.
    set(RUNTIME_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/CastlePatches/runtime")
    set(RUNTIME_X86_BINARY_DIR "${CMAKE_BINARY_DIR}/runtime_x86")
    set(RUNTIME_X86_DLL "${RUNTIME_X86_BINARY_DIR}/$<CONFIG>/CastleRuntime.dll")
    add_custom_command(
        OUTPUT "${RUNTIME_X86_DLL}"
        COMMAND "${CMAKE_COMMAND}" -S "${RUNTIME_SOURCE_DIR}" -B "${RUNTIME_X86_BINARY_DIR}" -G "${CMAKE_GENERATOR}" -A Win32
        COMMAND "${CMAKE_COMMAND}" --build "${RUNTIME_X86_BINARY_DIR}" --config "$<CONFIG>" --parallel
        DEPENDS src/CastlePatches/runtime/runtime_dll.c
                src/CastlePatches/runtime/CastleRuntime.def
                src/CastlePatches/runtime/CMakeLists.txt
        VERBATIM
    )
    add_custom_target(CastleRuntime ALL DEPENDS "${RUNTIME_X86_DLL}")
    add_dependencies(CastlePatches CastleRuntime)
    add_custom_command(TARGET CastlePatches POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${RUNTIME_X86_DLL}"
                "$<TARGET_FILE_DIR:CastlePatches>/CastleRuntime.dll"
        VERBATIM
    )
else()
    add_library(CastleRuntime SHARED src/CastlePatches/runtime/runtime_dll.c)
endif()

target_compile_definitions(CastlePatches PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(CastlePatches PRIVATE /W4 /permissive- /utf-8)
    # The explicit RC manifest contains the Common Controls v6 dependency.
    # Disable MSVC's second auto-generated manifest to avoid duplicate
    # RT_MANIFEST resources at link time.
    target_link_options(CastlePatches PRIVATE /MANIFEST:NO)
else()
    target_compile_options(CastlePatches PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(CastlePatches PRIVATE comdlg32 comctl32)
