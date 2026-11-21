cmake_minimum_required(VERSION 3.22)
project(OcarinaAndroid LANGUAGES CXX)

find_package(oboe REQUIRED CONFIG)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../../../../src" "${CMAKE_CURRENT_BINARY_DIR}/core")

add_library(ocarina_android SHARED AudioOutput.cpp)
target_link_libraries(ocarina_android PRIVATE ocarina_core oboe::oboe)
