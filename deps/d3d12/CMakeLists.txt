#
# D3D12-specific dependencies
#

add_subdirectory(directxagility)
add_subdirectory(directxcompiler)

set(D3D12MA_AGILITY_SDK_DIRECTORY ${AGILITYSDK_DIR})
FetchContent_Declare(d3d12_memory_allocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator
    GIT_TAG        "v3.0.1")

FetchContent_MakeAvailable(
    d3d12_memory_allocator)
