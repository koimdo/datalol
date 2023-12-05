include(ExternalProject)
# In order to keep the working tree clear from any external projects (so git-grep will not
# search in them, etc.), external projects are stored as compressed archives.
# If possible, such archives are generated from a git commit/tag by `git-archive`.
# Revision information can be extracted, by running:
# gunzip -c external.tar.gz | git get-tar-commit-id

ExternalProject_Add(googletest
  URL    "${CMAKE_CURRENT_SOURCE_DIR}/googletest.tar.gz"
  PREFIX "${CMAKE_BINARY_DIR}/googletest"
  EXCLUDE_FROM_ALL on
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -Dgtest_disable_pthreads=1 -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
)

ExternalProject_Add(jsoncpp
  URL    "${CMAKE_CURRENT_SOURCE_DIR}/jsoncpp-1.9.5.tar.gz"
  PREFIX "${CMAKE_BINARY_DIR}/jsoncpp"
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DBUILD_SHARED_LIBS=OFF -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
)
