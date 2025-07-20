#
# Eigen
#

# Eigen does come with a CMakeLists.txt but it's not intended for library use
FetchContent_Declare(eigen_fetch
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG        "3.4.0")
FetchContent_GetProperties(eigen_fetch)
if(NOT eigen_fetch_POPULATED)
    FetchContent_Populate(eigen_fetch)

    add_library(eigen INTERFACE)
    target_include_directories(eigen INTERFACE ${eigen_fetch_SOURCE_DIR})
endif()
