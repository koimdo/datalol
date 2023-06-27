cmake_minimum_required(VERSION 3.10)
project(Datalol)

include_directories("${PROJECT_SOURCE_DIR}/include")

add_subdirectory(src)
add_subdirectory(unittest)
