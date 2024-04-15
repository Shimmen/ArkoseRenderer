cmake_minimum_required(VERSION 3.7)
project(DirectXShaderCompiler)

FetchContent_Declare(dxc_fetch URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2403.1/dxc_2024_03_22.zip)
FetchContent_MakeAvailable(dxc_fetch)

add_library(dxc SHARED IMPORTED GLOBAL)
target_include_directories(dxc INTERFACE "${dxc_fetch_SOURCE_DIR}/inc/")

set_property(TARGET dxc PROPERTY IMPORTED_IMPLIB   "${dxc_fetch_SOURCE_DIR}/lib/x64/dxcompiler.lib")
set_property(TARGET dxc PROPERTY IMPORTED_LOCATION "${dxc_fetch_SOURCE_DIR}/bin/x64/dxcompiler.dll"
                                                    "${dxc_fetch_SOURCE_DIR}/bin/x64/dxil.dll")
