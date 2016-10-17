cmake_minimum_required(VERSION 2.8)

include(ExternalProject)

# Download/build gtest + gmock
ExternalProject_Add(
  ext_gtest_gmock
  URL https://github.com/google/googletest/archive/release-1.8.0.zip
  URL_HASH SHA256=f3ed3b58511efd272eb074a3a6d6fb79d7c2e6a0e374323d1e6bcbcc1ef141bf
  PREFIX ${CMAKE_CURRENT_BINARY_DIR}/gtest_gmock
  INSTALL_COMMAND "")
ExternalProject_Get_Property(ext_gtest_gmock source_dir binary_dir)

set(GMOCK_SOURCE_DIR ${source_dir}/googlemock)
set(GMOCK_BUILD_DIR ${binary_dir}/googlemock)
set(GMOCK_INCLUDE_DIR ${GMOCK_SOURCE_DIR}/include)
add_library(gmock STATIC IMPORTED GLOBAL)
add_dependencies(gmock ext_gtest_gmock)
set_target_properties(gmock PROPERTIES
  "IMPORTED_LOCATION" "${GMOCK_BUILD_DIR}/libgmock.a"
  "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}")
message(STATUS "GMock: ${GMOCK_BUILD_DIR}/libgmock.a")

set(GTEST_SOURCE_DIR ${source_dir}/googletest)
set(GTEST_BUILD_DIR ${binary_dir}/googletest)
set(GTEST_INCLUDE_DIR ${GTEST_SOURCE_DIR}/include)
add_library(gtest STATIC IMPORTED GLOBAL gtest/gtest-all.cc gtest/gtest.h)
add_dependencies(gtest ext_gtest_gmock)
# libgtest.a is in googlemock/gtest/ for some reason:
set_target_properties(gtest PROPERTIES
  "IMPORTED_LOCATION" "${GMOCK_BUILD_DIR}/gtest/libgtest.a"
  "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}")
message(STATUS "GTest: ${GMOCK_BUILD_DIR}/gtest/libgtest.a")