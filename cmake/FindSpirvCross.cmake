#.rst:
# FindSpirvCross
# ----------
#
# Try to find SpirvCross as part of the LunarG Vulkan SDK
#
# IMPORTED Targets
# ^^^^^^^^^^^^^^^^
#
# This module defines :prop_tgt:`IMPORTED` target ``SpirvCross::SpirvCross``, if
# SpirvCross has been found.
#
# Result Variables
# ^^^^^^^^^^^^^^^^
#
# This module defines the following variables::
#
#   SpirvCross_FOUND          - True if SpirvCross was found
#   SpirvCross_INCLUDE_DIRS   - include directories for SpirvCross
#   SpirvCross_LIBRARIES      - link against this library to use SpirvCross
#
# The module will also define two cache variables::
#
#   SpirvCross_INCLUDE_DIR    - the SpirvCross include directory
#   SpirvCross_LIBRARY        - the path to the SpirvCross library
#

set(SpirvCross_LIB_NAME spirv-cross-core spirv-cross-glsl spirv-cross-hlsl spirv-cross-msl spirv-cross-reflect spirv-cross-util)

if(WIN32)

  if(${CMAKE_BUILD_TYPE} MATCHES Debug)
    set(SpirvCross_LIB_NAME spirv-cross-cored spirv-cross-glsld spirv-cross-hlsld spirv-cross-msld spirv-cross-reflectd spirv-cross-utild)
  endif()

  find_path(SpirvCross_INCLUDE_DIR
    NAMES spirv_cross/spirv_cross.hpp
    PATHS "$ENV{VULKAN_SDK}/Include")
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    find_library(SpirvCross_LIBRARY
      NAMES ${SpirvCross_LIB_NAME}
      PATHS "$ENV{VULKAN_SDK}/Lib")
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    find_library(SpirvCross_LIBRARY
      NAMES ${SpirvCross_LIB_NAME}
      PATHS "$ENV{VULKAN_SDK}/Lib32")
  endif()
else()
  find_path(SpirvCross_INCLUDE_DIR
    NAMES spirv-cross/spirv_cross.hpp
    PATHS "$ENV{VULKAN_SDK}/include")
  find_library(SpirvCross_LIBRARY
    NAMES ${SpirvCross_LIB_NAME}
    PATHS "$ENV{VULKAN_SDK}/lib")
endif()

set(SpirvCross_LIBRARIES ${SpirvCross_LIBRARY})
set(SpirvCross_INCLUDE_DIRS ${SpirvCross_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SpirvCross
  DEFAULT_MSG
  SpirvCross_LIBRARY SpirvCross_INCLUDE_DIR)

mark_as_advanced(SpirvCross_INCLUDE_DIR SpirvCross_LIBRARY)

if(SpirvCross_FOUND AND NOT TARGET SpirvCross::SpirvCross)
  add_library(SpirvCross::SpirvCross UNKNOWN IMPORTED)
  set_target_properties(SpirvCross::SpirvCross PROPERTIES
    IMPORTED_LOCATION "${SpirvCross_LIBRARIES}"
    INTERFACE_INCLUDE_DIRECTORIES "${SpirvCross_INCLUDE_DIRS}")
endif()
