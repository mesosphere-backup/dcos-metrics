cmake_minimum_required(VERSION 2.8)

project(stats-slave-tests)

# Defines ${GTEST_INCLUDE_DIR}/${GMOCK_INCLUDE_DIR} and "gtest"/"gmock" libs
include(InstallGTestGMock.cmake)

include_directories(
  ${CMAKE_SOURCE_DIR}
  ${GMOCK_INCLUDE_DIR}
  ${GTEST_INCLUDE_DIR}
  )

add_executable(input_assigner_factory_tests input_assigner_factory_tests.cpp)
target_link_libraries(input_assigner_factory_tests gtest stats-slave)
add_test(input_assigner_factory_tests input_assigner_factory_tests)

add_executable(input_assigner_tests input_assigner_tests.cpp)
target_link_libraries(input_assigner_tests gmock gtest stats-slave)
add_test(input_assigner_tests input_assigner_tests)

add_executable(isolator_module_tests isolator_module_tests.cpp)
target_link_libraries(isolator_module_tests gmock gtest stats-slave)
add_test(isolator_module_tests isolator_module_tests)

add_executable(params_tests params_tests.cpp)
target_link_libraries(params_tests gtest stats-slave)
add_test(params_tests params_tests)

add_executable(port_reader_impl_fuzz port_reader_impl_fuzz.cpp)
target_link_libraries(port_reader_impl_fuzz gmock gtest stats-slave)
# disabled by default
#add_test(port_reader_impl_fuzz port_reader_impl_fuzz)

add_executable(port_reader_impl_tests port_reader_impl_tests.cpp)
target_link_libraries(port_reader_impl_tests gmock gtest stats-slave)
add_test(port_reader_impl_tests port_reader_impl_tests)

add_executable(port_runner_impl_tests port_runner_impl_tests.cpp)
target_link_libraries(port_runner_impl_tests gmock gtest stats-slave)
add_test(port_runner_impl_tests port_runner_impl_tests)

add_executable(port_writer_tests port_writer_tests.cpp)
target_link_libraries(port_writer_tests gtest stats-slave)
add_test(port_writer_tests port_writer_tests)

add_executable(range_pool_tests range_pool_tests.cpp)
target_link_libraries(range_pool_tests gtest stats-slave)
add_test(range_pool_tests range_pool_tests)

add_executable(sync_util_tests sync_util_tests.cpp)
target_link_libraries(sync_util_tests gtest stats-slave)
add_test(sync_util_tests sync_util_tests)

add_executable(tag_datadog_tests tag_datadog_tests.cpp)
target_link_libraries(tag_datadog_tests gtest stats-slave)
add_test(tag_datadog_tests tag_datadog_tests)

add_executable(tag_key_prefix_tests tag_key_prefix_tests.cpp)
target_link_libraries(tag_key_prefix_tests gtest stats-slave)
add_test(tag_key_prefix_tests tag_key_prefix_tests)