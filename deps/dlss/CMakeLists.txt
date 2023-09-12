cmake_minimum_required(VERSION 3.26)
project(DLSS)

#
# TODO: Use the include files & prebuilt binaries from GitHub: https://github.com/NVIDIA/DLSS
#       instead of depending on the user dowloading it and placing it in the correct folder.
#
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/nvngx_dlss_sdk/include/nvsdk_ngx.h")
	message(STATUS "Found DLSS SDK")
	set(DLSS_AVAILABLE ON CACHE INTERNAL "")
else()
	set(DLSS_AVAILABLE OFF CACHE INTERNAL "")
	message(STATUS "Could not find DLSS SDK")
	return()
endif()

add_library(dlss SHARED IMPORTED GLOBAL)
target_include_directories(dlss INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/nvngx_dlss_sdk/include/")

if(MSVC)
	set_property(TARGET dlss PROPERTY IMPORTED_LOCATION       "${CMAKE_CURRENT_SOURCE_DIR}/nvngx_dlss_sdk/lib/Windows_x86_64/rel/nvngx_dlss.dll")
	set_property(TARGET dlss PROPERTY IMPORTED_IMPLIB         "${CMAKE_CURRENT_SOURCE_DIR}/nvngx_dlss_sdk/lib/Windows_x86_64/x86_64/nvsdk_ngx_d.lib")
	set_property(TARGET dlss PROPERTY IMPORTED_IMPLIB_DEBUG   "${CMAKE_CURRENT_SOURCE_DIR}/nvngx_dlss_sdk/lib/Windows_x86_64/x86_64/nvsdk_ngx_d_dbg.lib")
else()
	# TODO: Implement for other platforms!
endif()
