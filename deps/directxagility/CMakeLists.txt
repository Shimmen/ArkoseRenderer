cmake_minimum_required(VERSION 3.26)
project(DirectXAgilitySDK)

if(MSVC)

    set(AGILITYSDK_VERSION_MAJOR "613" CACHE INTERNAL "")
    set(AGILITYSDK_VERSION_MINOR "1"   CACHE INTERNAL "")
    set(AGILITYSDK_NUGET_URL "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/1.${AGILITYSDK_VERSION_MAJOR}.${AGILITYSDK_VERSION_MINOR}")

    FetchContent_Declare(agilitysdk_fetch URL ${AGILITYSDK_NUGET_URL})
    FetchContent_MakeAvailable(agilitysdk_fetch)

    add_library(agilitysdk INTERFACE)
    target_include_directories(agilitysdk INTERFACE "${agilitysdk_fetch_SOURCE_DIR}/build/native/include/")

    set(AGILITYSDK_D3D12CORE_DLL      "${agilitysdk_fetch_SOURCE_DIR}/build/native/bin/x64/D3D12Core.dll"      CACHE INTERNAL "")
    set(AGILITYSDK_D3D12SDKLAYERS_DLL "${agilitysdk_fetch_SOURCE_DIR}/build/native/bin/x64/d3d12SDKLayers.dll" CACHE INTERNAL "")

endif()
