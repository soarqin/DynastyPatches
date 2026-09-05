include(CastlePatchesVersion)
project(CastlePatches VERSION ${CASTLEPATCHES_VERSION} LANGUAGES C)

set(CASTLEPATCHES_FILE_TYPE VFT_APP)
set(CASTLEPATCHES_FILE_DESCRIPTION "Castle Patches Launcher")
set(CASTLEPATCHES_INTERNAL_NAME "CastlePatches")
set(CASTLEPATCHES_ORIGINAL_FILENAME "CastlePatches.exe")
configure_file(CastlePatchesVersion.rc.in CastlePatchesVersion.rc @ONLY)

# NASM assembles both the runtime hooks and the embedded loader stub.
find_program(NASM_EXECUTABLE NAMES nasm)
if(NOT NASM_EXECUTABLE)
    message(FATAL_ERROR
        "NASM is required for all toolchains. "
        "Install it with 'winget install nasm', 'scoop install nasm' or 'choco install nasm'.")
endif()

# The remote loader stub is 32-bit x86 code for the game process. Assemble it
# to a flat binary and embed it as a generated C array so the launcher works
# identically for Win32 and x64 builds.
set(LOADER_STUB_DIR "${CMAKE_CURRENT_BINARY_DIR}/loader_stub")
set(LOADER_STUB_BIN "${LOADER_STUB_DIR}/runtime_loader_stub.bin")
set(LOADER_STUB_C "${LOADER_STUB_DIR}/runtime_loader_stub_blob.c")
set(LOADER_STUB_H "${LOADER_STUB_DIR}/runtime_loader_stub_blob.h")
add_custom_command(
    OUTPUT "${LOADER_STUB_C}" "${LOADER_STUB_H}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${LOADER_STUB_DIR}"
    COMMAND "${NASM_EXECUTABLE}" -f bin
            "-i${CMAKE_CURRENT_SOURCE_DIR}/"
            -o "${LOADER_STUB_BIN}"
            "${CMAKE_CURRENT_SOURCE_DIR}/runtime_loader_stub.asm"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_FILE=${LOADER_STUB_BIN}"
            "-DOUTPUT_C=${LOADER_STUB_C}"
            "-DOUTPUT_H=${LOADER_STUB_H}"
            -P "${CMAKE_SOURCE_DIR}/cmake/BinToC.cmake"
    DEPENDS runtime_loader_stub.asm
            runtime_loader_stub.inc
            "${CMAKE_SOURCE_DIR}/cmake/BinToC.cmake"
    BYPRODUCTS "${LOADER_STUB_BIN}"
    VERBATIM
)

add_executable(CastlePatches WIN32
    main.c
    CastlePatches.rc
    "${CMAKE_CURRENT_BINARY_DIR}/CastlePatchesVersion.rc"
    "${LOADER_STUB_C}"
)

# RPG.exe is a 32-bit process. Build the injected runtime DLL with a separate
# x86 project even when the launcher itself is x64.
set(RUNTIME_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
set(RUNTIME_X86_BINARY_DIR "${CMAKE_BINARY_DIR}/runtime_x86")
if(MSVC)
    set(RUNTIME_X86_DLL "${RUNTIME_X86_BINARY_DIR}/$<CONFIG>/CastleRuntime.dll")
else()
    set(RUNTIME_X86_DLL "${RUNTIME_X86_BINARY_DIR}/CastleRuntime.dll")
endif()
set(RUNTIME_CONFIGURE_COMMAND
    "${CMAKE_COMMAND}" -S "${RUNTIME_SOURCE_DIR}" -B "${RUNTIME_X86_BINARY_DIR}"
    -G "${CMAKE_GENERATOR}"
)
if(MSVC)
    list(APPEND RUNTIME_CONFIGURE_COMMAND -A Win32)
else()
    list(APPEND RUNTIME_CONFIGURE_COMMAND
        "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
        "-DCMAKE_C_FLAGS=-m32"
        "-DCMAKE_SHARED_LINKER_FLAGS=-m32"
    )
endif()
list(APPEND RUNTIME_CONFIGURE_COMMAND
    "-DCMAKE_ASM_NASM_COMPILER:FILEPATH=${NASM_EXECUTABLE}"
)
add_custom_command(
    OUTPUT "${RUNTIME_X86_DLL}"
    COMMAND ${RUNTIME_CONFIGURE_COMMAND}
    COMMAND "${CMAKE_COMMAND}" --build "${RUNTIME_X86_BINARY_DIR}" --config "$<CONFIG>" --parallel
    DEPENDS runtime/runtime_dll.c
            runtime/runtime_hooks.asm
            runtime/runtime_hook_addresses.inc
            runtime/CastleRuntime.def
            runtime/CMakeLists.txt
            "${CMAKE_SOURCE_DIR}/cmake/Dependencies.cmake"
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

target_compile_definitions(CastlePatches PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_include_directories(CastlePatches PRIVATE "${LOADER_STUB_DIR}")

if(MSVC)
    target_compile_options(CastlePatches PRIVATE /W4 /permissive-)
    # The explicit RC manifest contains the Common Controls v6 dependency.
    # Disable MSVC's second auto-generated manifest to avoid duplicate
    # RT_MANIFEST resources at link time.
    target_link_options(CastlePatches PRIVATE /MANIFEST:NO)
else()
    target_compile_options(CastlePatches PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(CastlePatches PRIVATE -municode)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        target_link_libraries(CastlePatches PRIVATE "-Xlinker /MANIFEST:NO")
    endif()
endif()

target_link_libraries(CastlePatches PRIVATE comdlg32 comctl32)
set_target_properties(CastlePatches PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
