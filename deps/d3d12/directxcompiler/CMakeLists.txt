cmake_minimum_required(VERSION 3.7)
project(DirectXShaderCompiler)

FetchContent_Declare(dxc_fetch URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2505.1/dxc_2025_07_14.zip)
FetchContent_MakeAvailable(dxc_fetch)

add_library(dxc SHARED IMPORTED GLOBAL)
target_include_directories(dxc INTERFACE "${dxc_fetch_SOURCE_DIR}/inc/")
set_property(TARGET dxc PROPERTY IMPORTED_IMPLIB   "${dxc_fetch_SOURCE_DIR}/lib/x64/dxcompiler.lib")
set_property(TARGET dxc PROPERTY IMPORTED_LOCATION "${dxc_fetch_SOURCE_DIR}/bin/x64/dxcompiler.dll")

add_library(dxil SHARED IMPORTED GLOBAL)
target_include_directories(dxil INTERFACE "${dxc_fetch_SOURCE_DIR}/inc/")
set_property(TARGET dxil PROPERTY IMPORTED_IMPLIB   "${dxc_fetch_SOURCE_DIR}/lib/x64/dxil.lib")
set_property(TARGET dxil PROPERTY IMPORTED_LOCATION "${dxc_fetch_SOURCE_DIR}/bin/x64/dxil.dll")
