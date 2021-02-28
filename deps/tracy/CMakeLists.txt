cmake_minimum_required(VERSION 3.1)
project(tracy)

FetchContent_Declare(tracy_fetch
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG        "v0.7.6")
FetchContent_GetProperties(tracy_fetch)
if(NOT tracy_fetch_POPULATED)
    FetchContent_Populate(tracy_fetch)

    add_library(tracy INTERFACE)
    target_include_directories(tracy INTERFACE ${tracy_fetch_SOURCE_DIR})

    # TracyClient.cpp must be compiled as part of the main executable (can't be linked in) so we have to export its path
    set(TRACY_CLIENT_SOURCE "${tracy_fetch_SOURCE_DIR}/TracyClient.cpp" PARENT_SCOPE)

endif()
