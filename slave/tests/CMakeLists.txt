cmake_minimum_required(VERSION 2.8)

project(stats-slave-tests)

# Defines ${GTEST_INCLUDE_DIR}/${GMOCK_INCLUDE_DIR} and "gtest"/"gmock" libs
include(InstallGTestGMock.cmake)

include_directories(
  ${CMAKE_SOURCE_DIR}
  ${GMOCK_INCLUDE_DIR}
  ${GTEST_INCLUDE_DIR}
  )

add_executable(input_assigner_tests input_assigner_tests.cpp)
target_link_libraries(input_assigner_tests gmock gtest stats-slave)
add_test(input_assigner_tests input_assigner_tests)

add_executable(port_reader_impl_tests port_reader_impl_tests.cpp)
target_link_libraries(port_reader_impl_tests gmock gtest stats-slave)
add_test(port_reader_impl_tests port_reader_impl_tests)

add_executable(port_runner_impl_tests port_runner_impl_tests.cpp)
target_link_libraries(port_runner_impl_tests gmock gtest stats-slave)
add_test(port_runner_impl_tests port_runner_impl_tests)

add_executable(port_writer_tests port_writer_tests.cpp)
target_link_libraries(port_writer_tests gtest stats-slave)
add_test(port_writer_tests port_writer_tests)
