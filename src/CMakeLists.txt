set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wsuggest-final-types -Wsuggest-final-methods")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=suggest-override -Wshadow=local")

add_library(${DATALOL_LIB} STATIC
  # Please keep this list sorted
  datalol.cpp
  debug.cpp
  memory.cpp
)

add_dependencies(${DATALOL_LIB} jsoncpp)
target_link_libraries(${DATALOL_LIB} "-ljsoncpp")

install( TARGETS ${DATALOL_LIB}
  ARCHIVE DESTINATION lib PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/include/"
  DESTINATION include
  PATTERN "bits" EXCLUDE
)
