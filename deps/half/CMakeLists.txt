cmake_minimum_required(VERSION 3.27)
project(half)

add_library(half INTERFACE)
target_include_directories(half INTERFACE .)
