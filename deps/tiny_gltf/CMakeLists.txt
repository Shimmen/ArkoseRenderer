cmake_minimum_required(VERSION 3.0)

project(tiny_gltf LANGUAGES CXX)
add_library(tiny_gltf tiny_gltf.cpp)
target_include_directories(tiny_gltf PUBLIC .)

target_compile_definitions(tiny_gltf PRIVATE TINYGLTF_NO_EXTERNAL_IMAGE)
target_compile_definitions(tiny_gltf PRIVATE TINYGLTF_USE_CPP14)

target_link_libraries(tiny_gltf PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(tiny_gltf PRIVATE stb_image)
