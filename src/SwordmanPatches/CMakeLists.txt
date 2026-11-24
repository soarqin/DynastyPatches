include(CastlePatchesVersion)
project(SwordmanPatches VERSION ${CASTLEPATCHES_VERSION} LANGUAGES C)

find_program(NASM_EXECUTABLE NAMES nasm REQUIRED)
set(LOADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/loader_stub")
set(LOADER_BIN "${LOADER_DIR}/runtime_loader_stub.bin")
set(LOADER_C "${LOADER_DIR}/runtime_loader_stub_blob.c")
set(LOADER_H "${LOADER_DIR}/runtime_loader_stub_blob.h")
add_custom_command(
    OUTPUT "${LOADER_C}" "${LOADER_H}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${LOADER_DIR}"
    COMMAND "${NASM_EXECUTABLE}" -f bin -i${CMAKE_CURRENT_SOURCE_DIR}/
            -o "${LOADER_BIN}" "${CMAKE_CURRENT_SOURCE_DIR}/runtime_loader_stub.asm"
    COMMAND "${CMAKE_COMMAND}" "-DINPUT_FILE=${LOADER_BIN}" "-DOUTPUT_C=${LOADER_C}"
            "-DOUTPUT_H=${LOADER_H}" -P "${CMAKE_SOURCE_DIR}/cmake/BinToC.cmake"
    DEPENDS runtime_loader_stub.asm runtime_loader_stub.inc "${CMAKE_SOURCE_DIR}/cmake/BinToC.cmake"
    VERBATIM
)

set(RUNTIME_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
set(RUNTIME_X86_BINARY_DIR "${CMAKE_BINARY_DIR}/swordman_runtime_x86")
if(MSVC)
    set(RUNTIME_X86_DLL "${RUNTIME_X86_BINARY_DIR}/$<CONFIG>/SwordmanRuntime.dll")
else()
    set(RUNTIME_X86_DLL "${RUNTIME_X86_BINARY_DIR}/SwordmanRuntime.dll")
endif()
set(RUNTIME_CONFIGURE_COMMAND "${CMAKE_COMMAND}" -S "${RUNTIME_SOURCE_DIR}" -B "${RUNTIME_X86_BINARY_DIR}" -G "${CMAKE_GENERATOR}")
if(MSVC)
    list(APPEND RUNTIME_CONFIGURE_COMMAND -A Win32)
else()
    list(APPEND RUNTIME_CONFIGURE_COMMAND "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}" "-DCMAKE_C_FLAGS=-m32" "-DCMAKE_SHARED_LINKER_FLAGS=-m32")
endif()
list(APPEND RUNTIME_CONFIGURE_COMMAND "-DCMAKE_ASM_NASM_COMPILER:FILEPATH=${NASM_EXECUTABLE}")
add_custom_command(
    OUTPUT "${RUNTIME_X86_DLL}"
    COMMAND ${RUNTIME_CONFIGURE_COMMAND}
    COMMAND "${CMAKE_COMMAND}" --build "${RUNTIME_X86_BINARY_DIR}" --config "$<CONFIG>" --parallel
    DEPENDS runtime/runtime_dll.c file_identity.c runtime/gameplay.c runtime/gameplay_hooks.asm
            runtime/gameplay_hook_addresses.inc
            file_identity.c runtime/display.c runtime/display_hooks.asm runtime/runtime_internal.h
             runtime/gameplay_addresses.h runtime/display_addresses.h swordman_addresses.h swordman.h
            runtime/SwordmanRuntime.def runtime/CMakeLists.txt
    VERBATIM
)
add_custom_target(SwordmanRuntime ALL DEPENDS "${RUNTIME_X86_DLL}")

if(BUILD_TESTING AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    # Visual Studio's NASM integration does not track %include dependencies.
    foreach(probe IN ITEMS extra_patch save_patch)
        add_custom_command(
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${probe}_test.obj"
            COMMAND "${NASM_EXECUTABLE}" -f win32
                    "-i${CMAKE_CURRENT_SOURCE_DIR}/" "-i${CMAKE_CURRENT_SOURCE_DIR}/runtime/"
                    -o "${CMAKE_CURRENT_BINARY_DIR}/${probe}_test.obj"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/${probe}_test.asm"
            DEPENDS "tests/${probe}_test.asm" runtime/gameplay_hooks.asm
                    runtime/gameplay_hook_addresses.inc
            VERBATIM
        )
    endforeach()
    set(EXTRA_TEST_BINARY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/SwordmanExtraPatchTest.exe")
    add_executable(SwordmanExtraPatchTest
        tests/extra_patch_test.c "${CMAKE_CURRENT_BINARY_DIR}/extra_patch_test.obj")
    target_include_directories(SwordmanExtraPatchTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
    target_compile_options(SwordmanExtraPatchTest PRIVATE
        $<$<COMPILE_LANGUAGE:C>:/W4>
        $<$<COMPILE_LANGUAGE:C>:/permissive->
        $<$<COMPILE_LANGUAGE:C>:/Od>)
    add_test(NAME SwordmanExtraPatchTest COMMAND "${EXTRA_TEST_BINARY}")

    add_executable(SwordmanSavePatchTest
        tests/save_patch_test.c "${CMAKE_CURRENT_BINARY_DIR}/save_patch_test.obj")
    target_compile_options(SwordmanSavePatchTest PRIVATE /W4 /permissive-)
    add_test(NAME SwordmanSavePatchTest COMMAND SwordmanSavePatchTest)
endif()

add_executable(SwordmanPatches WIN32 main.c ../launcher/launcher_ui.c file_identity.c SwordmanPatches.rc
               "${CMAKE_CURRENT_BINARY_DIR}/SwordmanPatchesVersion.rc" "${LOADER_C}")
add_dependencies(SwordmanPatches SwordmanRuntime)
add_custom_command(TARGET SwordmanPatches POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${RUNTIME_X86_DLL}"
            "$<TARGET_FILE_DIR:SwordmanPatches>/SwordmanRuntime.dll")
set(SWORDMANPATCHES_FILE_TYPE VFT_APP)
set(SWORDMANPATCHES_FILE_DESCRIPTION "Swordman Patches Launcher")
set(SWORDMANPATCHES_INTERNAL_NAME "SwordmanPatches")
set(SWORDMANPATCHES_ORIGINAL_FILENAME "SwordmanPatches.exe")
configure_file(SwordmanPatchesVersion.rc.in SwordmanPatchesVersion.rc @ONLY)
target_compile_definitions(SwordmanPatches PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    "LAUNCHER_VERSION=L\"${PROJECT_VERSION}\"")
target_include_directories(SwordmanPatches PRIVATE "${LOADER_DIR}")
target_link_libraries(SwordmanPatches PRIVATE comdlg32 comctl32 bcrypt)
if(MSVC)
    target_compile_options(SwordmanPatches PRIVATE /W4 /permissive-)
    target_link_options(SwordmanPatches PRIVATE /MANIFEST:NO)
else()
    target_compile_options(SwordmanPatches PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(SwordmanPatches PRIVATE -municode)
endif()
set_target_properties(SwordmanPatches PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
