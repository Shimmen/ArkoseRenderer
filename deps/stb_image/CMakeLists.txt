cmake_minimum_required(VERSION 3.26)

project(stb_image LANGUAGES C)

FetchContent_Declare(stb_fetch
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        "f58f558c120e9b32c217290b80bad1a0729fbb2c") # Latest as of 20 July 2025
FetchContent_GetProperties(stb_fetch)
if(NOT stb_fetch_POPULATED)
    FetchContent_Populate(stb_fetch)

    add_library(stb_image stb_image.c)
    target_include_directories(stb_image PUBLIC ${stb_fetch_SOURCE_DIR})

endif()
