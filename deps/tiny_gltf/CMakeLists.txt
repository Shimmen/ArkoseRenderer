cmake_minimum_required(VERSION 3.26)

project(tiny_gltf LANGUAGES CXX)

FetchContent_Declare(tinygltf_fetch
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG        "v2.9.6")
FetchContent_GetProperties(tinygltf_fetch)
if(NOT tinygltf_fetch_POPULATED)
    FetchContent_Populate(tinygltf_fetch)

    add_library(tiny_gltf
        "${tinygltf_fetch_SOURCE_DIR}/tiny_gltf.h"
        "tiny_gltf.cpp"
        )

    target_include_directories(tiny_gltf PUBLIC ${tinygltf_fetch_SOURCE_DIR})

    # Don't use stb_image for reading/writing images, we will handle that ourselves
    # Don't attempt to load images from external sources (i.e., image files), we will handle that ourselves
    target_compile_definitions(tiny_gltf PUBLIC
        # Don't use stb_image
        TINYGLTF_NO_STB_IMAGE
        TINYGLTF_NO_INCLUDE_STB_IMAGE 
        # Don't use stb_image_write
        TINYGLTF_NO_STB_IMAGE_WRITE
        TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE 
        # Don't load external images at all (we do our own image loading)
        TINYGLTF_NO_EXTERNAL_IMAGE)

    target_link_libraries(tiny_gltf PRIVATE nlohmann_json::nlohmann_json)

endif()
