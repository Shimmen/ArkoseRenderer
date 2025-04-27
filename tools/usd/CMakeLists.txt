
find_package(USD REQUIRED)

message(STATUS "PXR_USD_LOCATION=${PXR_USD_LOCATION}")
message(STATUS "PXR_INCLUDE_DIRS=${PXR_INCLUDE_DIRS}")
message(STATUS "PXR_LIBRARIES=${PXR_LIBRARIES}")

##################################
###### Command line tools ########
##################################

project(UsdImportTool)
add_executable(${PROJECT_NAME} UsdImportTool.cpp)
target_link_libraries(${PROJECT_NAME} ArkoseCore)
set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "tools")
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_LIST_DIR}/bin/usd")

target_include_directories(${PROJECT_NAME} PRIVATE ${PXR_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${PXR_LIBRARIES})

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:${PROJECT_NAME}> $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}> # copy DLLs to exe location
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PXR_USD_LOCATION}/lib/usd" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/usd" # copy USD plugins to exe location
    COMMAND_EXPAND_LISTS )
