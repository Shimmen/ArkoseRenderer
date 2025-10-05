
##################################
###### Command line tools ########
##################################

project(GltfImportTool)
add_executable(${PROJECT_NAME} GltfImportTool.cpp)
target_link_libraries(${PROJECT_NAME} ArkoseCore)
set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "tools")
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_LIST_DIR}/bin")

project(IESConvertTool)
add_executable(${PROJECT_NAME} IESConvertTool.cpp)
target_link_libraries(${PROJECT_NAME} ArkoseCore)
set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "tools")
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_LIST_DIR}/bin")

project(ArkAssetBakeTool)
add_executable(${PROJECT_NAME} ArkAssetBakeTool.cpp)
target_link_libraries(${PROJECT_NAME} ArkoseCore)
set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "tools")
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_LIST_DIR}/bin")

project(ImgAssetBakeTool)
add_executable(${PROJECT_NAME} ImgAssetBakeTool.cpp)
target_link_libraries(${PROJECT_NAME} ArkoseCore)
set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "tools")
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_LIST_DIR}/bin")

project(ShaderCompilerTool)
add_executable(${PROJECT_NAME} ShaderCompilerTool.cpp)
target_link_libraries(${PROJECT_NAME} ArkoseCore)
target_link_libraries(${PROJECT_NAME} slang_custom)
set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "tools")
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_LIST_DIR}/bin")

# Ensure DLLs are copied to the tools executable directory after build
# Use whatever tool target is created last, as they will all share their output directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:${PROJECT_NAME}> $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}>
    COMMAND_EXPAND_LISTS)

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
