cmake_minimum_required(VERSION 3.7)
project(slang)

if(${CMAKE_HOST_SYSTEM_NAME} MATCHES "Windows")
    set(slang_fetch_url https://github.com/shader-slang/slang/releases/download/v2025.18.1/slang-2025.18.1-windows-x86_64.zip)
elseif(${CMAKE_HOST_SYSTEM_NAME} MATCHES "Linux")
    set(slang_fetch_url https://github.com/shader-slang/slang/releases/download/v2025.18.1/slang-2025.18.1-linux-x86_64.zip)
endif()

FetchContent_Declare(slang_fetch URL ${slang_fetch_url})
FetchContent_MakeAvailable(slang_fetch)

# something about the provided slang CMake config seems to not work properly.. so let's do it manually, for now.
add_library(slang_custom SHARED IMPORTED GLOBAL)
target_include_directories(slang_custom INTERFACE "${slang_fetch_SOURCE_DIR}/include/")

if(${CMAKE_HOST_SYSTEM_NAME} MATCHES "Windows")
    set_property(TARGET slang_custom PROPERTY IMPORTED_IMPLIB   "${slang_fetch_SOURCE_DIR}/lib/slang.lib")
    set_property(TARGET slang_custom PROPERTY IMPORTED_LOCATION "${slang_fetch_SOURCE_DIR}/bin/slang.dll"
                                                                "${slang_fetch_SOURCE_DIR}/bin/slang-glsl-module.dll")
elseif(${CMAKE_HOST_SYSTEM_NAME} MATCHES "Linux")
    set_property(TARGET slang_custom PROPERTY IMPORTED_LOCATION "${slang_fetch_SOURCE_DIR}/lib/libslang.so")
                                                                # TODO! looks like gcc & clang don't like multiple IMPORTED_LOCATION
                                                                #"${slang_fetch_SOURCE_DIR}/lib/libslang-glsl-module.so")
endif()
