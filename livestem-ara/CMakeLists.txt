cmake_minimum_required(VERSION 3.22)

if(APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal Binary")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(LiveStemARA VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

option(LIVESTEM_BUILD_PLUGIN "Build the ARA plug-in (downloads JUCE and the ARA SDK)" ON)
option(LIVESTEM_BUILD_TESTS "Build the core unit tests" ON)

add_subdirectory(core)

if(LIVESTEM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(LIVESTEM_BUILD_PLUGIN)
    include(cmake/Dependencies.cmake)
    add_subdirectory(plugin)
    add_subdirectory(hosttest)
endif()
