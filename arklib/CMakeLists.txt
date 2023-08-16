cmake_minimum_required(VERSION 3.27)
project(arklib)

add_library(arklib INTERFACE)
target_include_directories(arklib INTERFACE include)

set_property(TARGET arklib PROPERTY CXX_STANDARD 20)

# Build tests if this is invoked directly from CMake (i.e., not through add_subdirectory)
if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
    add_subdirectory(tests)
endif ()
