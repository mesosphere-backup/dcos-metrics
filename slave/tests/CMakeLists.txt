cmake_minimum_required(VERSION 2.8)

project(stats-slave-tests)

# Defines ${GTEST_INCLUDE_DIR}/${GMOCK_INCLUDE_DIR} and "gtest"/"gmock" libs
include(InstallGTestGMock.cmake)

include_directories(
  ${CMAKE_SOURCE_DIR}
  ${GMOCK_INCLUDE_DIR}
  ${GTEST_INCLUDE_DIR}
  )

add_executable(env_hook_tests env_hook_tests.cpp)
target_link_libraries(env_hook_tests gmock gtest stats-slave)
add_test(env_hook_tests env_hook_tests)

add_executable(isolator_module_tests isolator_module_tests.cpp)
target_link_libraries(isolator_module_tests gmock gtest stats-slave)
add_test(isolator_module_tests isolator_module_tests)
