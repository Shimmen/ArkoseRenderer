cmake_minimum_required(VERSION 3.7)
project(DLSS)

set(DLSS_AVAILABLE ON CACHE INTERNAL "")

FetchContent_Declare(dlss_fetch
    GIT_REPOSITORY https://github.com/NVIDIA/DLSS
    GIT_TAG        "v3.7.10")
FetchContent_MakeAvailable(dlss_fetch)

add_library(dlss SHARED IMPORTED GLOBAL)
target_include_directories(dlss INTERFACE "${dlss_fetch_SOURCE_DIR}/include/")

if(MSVC)
	set_property(TARGET dlss PROPERTY IMPORTED_LOCATION       "${dlss_fetch_SOURCE_DIR}/lib/Windows_x86_64/dev/nvngx_dlss.dll") # TODO: Use /res/ for non-dev builds! Maybe all builds even?
	set_property(TARGET dlss PROPERTY IMPORTED_IMPLIB         "${dlss_fetch_SOURCE_DIR}/lib/Windows_x86_64/x86_64/nvsdk_ngx_d.lib")
	set_property(TARGET dlss PROPERTY IMPORTED_IMPLIB_DEBUG   "${dlss_fetch_SOURCE_DIR}/lib/Windows_x86_64/x86_64/nvsdk_ngx_d_dbg.lib")
else()
	# TODO: Implement for other platforms!
endif()
