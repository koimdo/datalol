set(TARGET main)

add_executable(${TARGET} main.cpp)
add_dependencies(${TARGET} datalol)
target_link_libraries(${TARGET} datalol)
