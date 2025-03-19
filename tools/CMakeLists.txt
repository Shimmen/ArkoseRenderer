
##################################
###### Command line tools ########
##################################

project(GltfImportTool)
add_executable(${PROJECT_NAME} GltfImportTool.cpp)
target_link_libraries(${PROJECT_NAME} ArkoseCore)
set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "tools")

##################################
## OpenUSD-specific tooling WIP ##
##################################

if((DEFINED {PXR_USD_LOCATION}) OR (DEFINED ENV{PXR_USD_LOCATION}))
    message(STATUS "Will attempt to build USD tools")
    add_subdirectory(usd)
else()
    message(STATUS "Will NOT attempt to build USD tools as PXR_USD_LOCATION is not set")
endif()

##################################
