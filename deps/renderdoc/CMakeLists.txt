cmake_minimum_required(VERSION 3.7)
project(renderdoc_api)

add_library(renderdoc_api INTERFACE)
target_include_directories(renderdoc_api INTERFACE .)

# TODO!
#find_file(RENDERDOC_SHARED_LIB_LOCATION
#    NAME  renderdoc.dll
#    PATHS "C:/Program Files/RenderDoc/" )
