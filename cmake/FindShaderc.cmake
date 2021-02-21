#.rst:
# FindShaderc
# ----------
#
# Try to find Shaderc as part of the LunarG Vulkan SDK
#
# IMPORTED Targets
# ^^^^^^^^^^^^^^^^
#
# This module defines :prop_tgt:`IMPORTED` target ``Shaderc::Shaderc``, if
# Shaderc has been found.
#
# Result Variables
# ^^^^^^^^^^^^^^^^
#
# This module defines the following variables::
#
#   Shaderc_FOUND          - True if Shaderc was found
#   Shaderc_INCLUDE_DIRS   - include directories for Shaderc
#   Shaderc_LIBRARIES      - link against this library to use Shaderc
#
# The module will also define two cache variables::
#
#   Shaderc_INCLUDE_DIR    - the Shaderc include directory
#   Shaderc_LIBRARY        - the path to the Shaderc library
#

if(WIN32)

  set(Shaderc_LIB_NAME "shaderc_combined")
  if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(Shaderc_LIB_NAME "${Shaderc_LIB_NAME}d")
  endif()

  find_path(Shaderc_INCLUDE_DIR
    NAMES shaderc/shaderc.h
    PATHS "$ENV{VULKAN_SDK}/Include")
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    find_library(Shaderc_LIBRARY
      NAMES ${Shaderc_LIB_NAME}
      PATHS "$ENV{VULKAN_SDK}/Lib")
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    find_library(Shaderc_LIBRARY
      NAMES ${Shaderc_LIB_NAME}
      PATHS "$ENV{VULKAN_SDK}/Lib32")
  endif()
else()
  find_path(Shaderc_INCLUDE_DIR
    NAMES shaderc/shaderc.h
    PATHS "$ENV{VULKAN_SDK}/include")
  find_library(Shaderc_LIBRARY
    NAMES shaderc_combined
    PATHS "$ENV{VULKAN_SDK}/lib")
endif()

set(Shaderc_LIBRARIES ${Shaderc_LIBRARY})
set(Shaderc_INCLUDE_DIRS ${Shaderc_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Shaderc
  DEFAULT_MSG
  Shaderc_LIBRARY Shaderc_INCLUDE_DIR)

mark_as_advanced(Shaderc_INCLUDE_DIR Shaderc_LIBRARY)

if(Shaderc_FOUND AND NOT TARGET Shaderc::Shaderc)
  add_library(Shaderc::Shaderc UNKNOWN IMPORTED)
  set_target_properties(Shaderc::Shaderc PROPERTIES
    IMPORTED_LOCATION "${Shaderc_LIBRARIES}"
    INTERFACE_INCLUDE_DIRECTORIES "${Shaderc_INCLUDE_DIRS}")
endif()
