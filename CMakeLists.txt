cmake_minimum_required(VERSION 3.20)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CustomCompilerOptions)

project(DynastyPatches)

add_subdirectory(src)
