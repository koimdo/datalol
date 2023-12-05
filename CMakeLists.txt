cmake_minimum_required(VERSION 3.10)
project(Datalol)

include_directories("${PROJECT_SOURCE_DIR}/include")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wsuggest-final-types -Wsuggest-final-methods")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=suggest-override -Wshadow=local")
add_subdirectory(src)
add_subdirectory(unittest)
