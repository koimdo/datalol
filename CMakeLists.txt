cmake_minimum_required(VERSION 3.10)
project(Datalol)

cmake_policy(SET CMP0079 NEW)

set(DATALOL_LIB datalol)

include_directories("${PROJECT_SOURCE_DIR}/include")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ftime-trace") # compile time tracing with clang

add_subdirectory(src)

if(EXISTS "${PROJECT_SOURCE_DIR}/.gitattributes")
  # In development worktree
  add_subdirectory(external)
  add_dependencies(${DATALOL_LIB} jsoncpp)
  set(JSON_ROOT "${CMAKE_BINARY_DIR}/jsoncpp")

  add_subdirectory(unittest)
else()
  set(JSON_ROOT "${CMAKE_PREFIX_PATH}")
endif()

target_include_directories(${DATALOL_LIB} PUBLIC "${JSON_ROOT}/include")
target_link_directories(${DATALOL_LIB} PUBLIC "${JSON_ROOT}/lib")
target_link_libraries(${DATALOL_LIB} "-ljsoncpp")
