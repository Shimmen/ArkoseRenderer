cmake_minimum_required(VERSION 3.7)
project(NvidiaNsightAftermathSDK)

# URLs from https://developer.nvidia.com/nsight-aftermath/getting-started in case the exact location changes or we want to change version
if (WIN32)
    set(AFTERMATH_SDK_SUPPORTED True CACHE INTERNAL "")
    set(AFTERMATH_SDK_URL "https://developer.nvidia.com/downloads/assets/tools/secure/nsight-aftermath-sdk/2025_4_0/windows/NVIDIA_Nsight_Aftermath_SDK_2025.4.0.25209.zip")
else()
    # TODO: Handle Linux, it is supported by the SDK
    set(AFTERMATH_SDK_SUPPORTED False CACHE INTERNAL "")
endif()

if (AFTERMATH_SDK_SUPPORTED)
    FetchContent_Declare(aftermath_sdk_fetch URL ${AFTERMATH_SDK_URL})
    FetchContent_MakeAvailable(aftermath_sdk_fetch)

    add_library(aftermath_sdk SHARED IMPORTED GLOBAL)
    target_include_directories(aftermath_sdk INTERFACE "${aftermath_sdk_fetch_SOURCE_DIR}/include/")

    set_property(TARGET aftermath_sdk PROPERTY IMPORTED_IMPLIB   "${aftermath_sdk_fetch_SOURCE_DIR}/lib/x64/GFSDK_Aftermath_Lib.x64.lib")
    set_property(TARGET aftermath_sdk PROPERTY IMPORTED_LOCATION "${aftermath_sdk_fetch_SOURCE_DIR}/lib/x64/GFSDK_Aftermath_Lib.x64.dll")
endif()
