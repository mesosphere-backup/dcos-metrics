cmake_minimum_required(VERSION 2.8)

project(stats-slave-tests)

# Defines ${GTEST_INCLUDE_DIR}/${GMOCK_INCLUDE_DIR} and "gtest"/"gmock" libs
include(InstallGTestGMock.cmake)

include_directories(
  ${CMAKE_SOURCE_DIR}
  ${GMOCK_INCLUDE_DIR}
  ${GTEST_INCLUDE_DIR}
  )

add_executable(params_tests params_tests.cpp)
target_link_libraries(params_tests gtest stats-slave)
add_test(params_tests params_tests)
