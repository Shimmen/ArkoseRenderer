
# USD is required for all this tooling to work, as we are a "USD native" asset pipeline
find_package(USD REQUIRED)

message(STATUS "PXR_USD_LOCATION=${PXR_USD_LOCATION}")
message(STATUS "PXR_INCLUDE_DIRS=${PXR_INCLUDE_DIRS}")
message(STATUS "PXR_LIBRARIES=${PXR_LIBRARIES}")

##################################
######### ASSET PROCESSOR ########
##################################

add_library(AssetProcessor STATIC
    assetprocessor/AssetProcessor.cpp
    )
target_include_directories(AssetProcessor PUBLIC
    assetprocessor
    ${PXR_INCLUDE_DIRS}
    )
target_link_libraries(AssetProcessor PUBLIC
    ArkoseCore
    ${PXR_LIBRARIES}
    mikktspace # for MikkTSpace tangent generation
    )

get_target_property(ASSETPROCESSOR_SOURCES AssetProcessor SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/assetprocessor" FILES ${ASSETPROCESSOR_SOURCES})

# Basic sample of the asset processor, for development purposes
add_executable(AssetProcessorSample assetprocessor/sample/main.cpp)
target_link_libraries(AssetProcessorSample PRIVATE AssetProcessor)

add_custom_command(TARGET AssetProcessorSample POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:AssetProcessorSample> $<TARGET_RUNTIME_DLLS:AssetProcessorSample> # copy DLLs to exe
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PXR_USD_LOCATION}/lib/usd" "$<TARGET_FILE_DIR:AssetProcessorSample>/usd" # copy USD plugins to exe
    COMMAND_EXPAND_LISTS
)

##################################
########## ASSET SERVER ##########
##################################
#[[

add_executable(AssetServer
    assetserver/AssetServer.cpp
    )
target_link_libraries(AssetServer PRIVATE
    AssetProcessor
    )

get_target_property(ASSETSERVER_SOURCES AssetServer SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/assetserver" FILES ${ASSETSERVER_SOURCES})

]]
##################################
