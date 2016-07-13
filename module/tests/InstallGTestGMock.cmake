cmake_minimum_required(VERSION 2.8)

include(ExternalProject)

# Download/build gtest
ExternalProject_Add(
  ext_gtest
  URL https://googletest.googlecode.com/files/gtest-1.7.0.zip
  URL_HASH SHA256=247ca18dd83f53deb1328be17e4b1be31514cedfc1e3424f672bf11fd7e0d60d
  PREFIX ${CMAKE_CURRENT_BINARY_DIR}/gtest
  INSTALL_COMMAND "")
add_library(gtest STATIC IMPORTED GLOBAL gtest/gtest-all.cc gtest/gtest.h)
add_dependencies(gtest ext_gtest)
ExternalProject_Get_Property(ext_gtest source_dir binary_dir)
set(GTEST_SOURCE_DIR ${source_dir})
set(GTEST_BUILD_DIR ${binary_dir})
set(GTEST_INCLUDE_DIR ${GTEST_SOURCE_DIR}/include)
set_target_properties(gtest PROPERTIES
  "IMPORTED_LOCATION" "${GTEST_BUILD_DIR}/libgtest.a"
  "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}")
message(STATUS "GTest: ${GTEST_BUILD_DIR}/libgtest.a")

# Download/build gmock (not yet packaged in gtest distributions)
ExternalProject_Add(
  ext_gmock
  URL https://googlemock.googlecode.com/files/gmock-1.7.0.zip
  URL_HASH SHA256=26fcbb5925b74ad5fc8c26b0495dfc96353f4d553492eb97e85a8a6d2f43095b
  PREFIX ${CMAKE_CURRENT_BINARY_DIR}/gmock
  INSTALL_COMMAND "")
add_library(gmock STATIC IMPORTED GLOBAL)
add_dependencies(gmock ext_gmock)
ExternalProject_Get_Property(ext_gmock source_dir binary_dir)
set(GMOCK_SOURCE_DIR ${source_dir})
set(GMOCK_BUILD_DIR ${binary_dir})
set(GMOCK_INCLUDE_DIR ${GMOCK_SOURCE_DIR}/include)
set_target_properties(gmock PROPERTIES
  "IMPORTED_LOCATION" "${GMOCK_BUILD_DIR}/libgmock.a"
  "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}")
message(STATUS "GMock: ${GMOCK_BUILD_DIR}/libgmock.a")