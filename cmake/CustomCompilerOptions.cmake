set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_options(
    $<$<AND:$<NOT:$<COMPILE_LANGUAGE:ASM_NASM>>,$<OR:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:/GL\;/utf-8>
    $<$<AND:$<NOT:$<COMPILE_LANGUAGE:ASM_NASM>>,$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:-flto>
)
add_link_options(
    $<$<AND:$<NOT:$<COMPILE_LANGUAGE:ASM_NASM>>,$<C_COMPILER_ID:MSVC>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:/LTCG>
    $<$<AND:$<NOT:$<COMPILE_LANGUAGE:ASM_NASM>>,$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:-flto\;-s>
)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_link_options(
    $<$<AND:$<NOT:$<COMPILE_LANGUAGE:ASM_NASM>>,$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:-static>
    $<$<AND:$<NOT:$<COMPILE_LANGUAGE:ASM_NASM>>,$<C_COMPILER_ID:GNU>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:-static-libgcc>
    $<$<AND:$<NOT:$<COMPILE_LANGUAGE:ASM_NASM>>,$<CXX_COMPILER_ID:GNU>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:-static-libstdc++>
)
