
# TODO: Move the `add_subdirectory` calls for dxcompiler & agility SDK into here,
# and also move their actual subdirectories to under this one, d3d12/

set(D3D12MA_AGILITY_SDK_DIRECTORY ${AGILITYSDK_DIR})
FetchContent_Declare(d3d12_memory_allocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator
    GIT_TAG        "0a9cbbbdb0da95be059f6c40a958188bfbd96b9e" # last release is very old..
    ) 

FetchContent_MakeAvailable(
    d3d12_memory_allocator)
