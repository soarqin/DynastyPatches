cmake_minimum_required(VERSION 3.20)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CustomCompilerOptions)

project(DynastyPatches)

option(BUILD_TESTING "Build and register tests" OFF)
if(BUILD_TESTING)
    enable_testing()
endif()

add_subdirectory(src)
