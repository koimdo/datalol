set(GTEST_PREFIX "${CMAKE_BINARY_DIR}/googletest")

enable_testing()
add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure)

link_directories("${GTEST_PREFIX}/lib") # target_link_directories requires CMake > 3.13
function(add_suite EXE_NAME)
  add_executable(${EXE_NAME} EXCLUDE_FROM_ALL "${EXE_NAME}.cpp")
  add_dependencies(${EXE_NAME} googletest)
  target_include_directories(${EXE_NAME} PUBLIC "${GTEST_PREFIX}/include")
  target_link_libraries(${EXE_NAME} gtest_main gtest ${DATALOL_LIB})
  add_test(check_${EXE_NAME} ${EXE_NAME})
  add_dependencies(check ${EXE_NAME})
endfunction(add_suite)

add_suite(trivial)

add_suite(test_debug)
