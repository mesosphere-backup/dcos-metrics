cmake_minimum_required(VERSION 2.8)

include(ExternalProject)

# Download/build gtest
ExternalProject_Add(
  ext_gtest
  URL https://googletest.googlecode.com/files/gtest-1.7.0.zip
  PREFIX ${CMAKE_CURRENT_BINARY_DIR}/gtest
  INSTALL_COMMAND ""
  )
add_library(gtest IMPORTED STATIC GLOBAL)
add_dependencies(gtest ext_gtest)
ExternalProject_Get_Property(ext_gtest source_dir binary_dir)
set(GTEST_SOURCE_DIR ${source_dir})
set(GTEST_BUILD_DIR ${binary_dir})
set(GTEST_INCLUDE_DIR ${GTEST_SOURCE_DIR}/include)
set_target_properties(gtest PROPERTIES
  "IMPORTED_LOCATION" "${GTEST_BUILD_DIR}/libgtest.a"
  "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}"
  )

# Download/build gmock (not yet packaged in gtest distributions)
ExternalProject_Add(
  ext_gmock
  URL https://googlemock.googlecode.com/files/gmock-1.7.0.zip
  PREFIX ${CMAKE_CURRENT_BINARY_DIR}/gmock
  INSTALL_COMMAND ""
  )
add_library(gmock IMPORTED STATIC GLOBAL)
add_dependencies(gmock ext_gmock)
ExternalProject_Get_Property(ext_gmock source_dir binary_dir)
set(GMOCK_SOURCE_DIR ${source_dir})
set(GMOCK_BUILD_DIR ${binary_dir})
set(GMOCK_INCLUDE_DIR ${GMOCK_SOURCE_DIR}/include)
set_target_properties(gmock PROPERTIES
  "IMPORTED_LOCATION" "${GMOCK_BUILD_DIR}/libgmock.a"
  "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}"
  )