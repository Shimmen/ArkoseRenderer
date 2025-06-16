cmake_minimum_required(VERSION 3.7)
project(OpacityMicroMapSDK)

FetchContent_Declare(omm_sdk_fetch URL https://github.com/NVIDIA-RTX/OMM/releases/download/v1.8.0/Opacity.Micro-Map.SDK.Windows.zip)
FetchContent_MakeAvailable(omm_sdk_fetch)

add_library(omm_sdk SHARED IMPORTED GLOBAL)
target_include_directories(omm_sdk INTERFACE "${omm_sdk_fetch_SOURCE_DIR}/include/")
set_property(TARGET omm_sdk PROPERTY IMPORTED_IMPLIB   "${omm_sdk_fetch_SOURCE_DIR}/lib/omm-lib.lib")
set_property(TARGET omm_sdk PROPERTY IMPORTED_LOCATION "${omm_sdk_fetch_SOURCE_DIR}/bin/omm-lib.dll")
