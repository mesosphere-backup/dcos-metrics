cmake_minimum_required(VERSION 2.8)

project(metrics-module-tests)

# Defines ${GTEST_INCLUDE_DIR}/${GMOCK_INCLUDE_DIR} and "gtest"/"gmock" libs
include(InstallGTestGMock.cmake)

include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR}) # for metrics schema
# for library headers, hide any warnings:
include_directories(SYSTEM ${GMOCK_INCLUDE_DIR} ${GTEST_INCLUDE_DIR})

add_executable(avro_encoder_tests avro_encoder_tests.cpp)
target_link_libraries(avro_encoder_tests gmock gtest metrics-module)
add_test(avro_encoder_tests avro_encoder_tests)

add_executable(container_assigner_tests container_assigner_tests.cpp)
target_link_libraries(container_assigner_tests gmock gtest metrics-module)
add_test(container_assigner_tests container_assigner_tests)

add_executable(container_assigner_strategy_tests container_assigner_strategy_tests.cpp)
target_link_libraries(container_assigner_strategy_tests gmock gtest metrics-module)
add_test(container_assigner_strategy_tests container_assigner_strategy_tests)

add_executable(container_reader_impl_tests container_reader_impl_tests.cpp)
target_link_libraries(container_reader_impl_tests gmock gtest metrics-module)
add_test(container_reader_impl_tests container_reader_impl_tests)

add_executable(container_state_cache_impl_tests container_state_cache_impl_tests.cpp)
target_link_libraries(container_state_cache_impl_tests gmock gtest metrics-module)
add_test(container_state_cache_impl_tests container_state_cache_impl_tests)

add_executable(io_runner_impl_tests io_runner_impl_tests.cpp)
target_link_libraries(io_runner_impl_tests gmock gtest metrics-module)
add_test(io_runner_impl_tests io_runner_impl_tests)

add_executable(isolator_module_tests isolator_module_tests.cpp)
target_link_libraries(isolator_module_tests gmock gtest metrics-module)
add_test(isolator_module_tests isolator_module_tests)

add_executable(memnmem_tests memnmem_tests.cpp)
target_link_libraries(memnmem_tests gtest metrics-module)
add_test(memnmem_tests memnmem_tests)

add_executable(module_access_factory_tests module_access_factory_tests.cpp)
target_link_libraries(module_access_factory_tests gtest metrics-module)
add_test(module_access_factory_tests module_access_factory_tests)

add_executable(params_tests params_tests.cpp)
target_link_libraries(params_tests gtest metrics-module)
add_test(params_tests params_tests)

add_executable(range_pool_tests range_pool_tests.cpp)
target_link_libraries(range_pool_tests gtest metrics-module)
add_test(range_pool_tests range_pool_tests)

add_executable(sample_translator sample_translator.cpp)
target_link_libraries(sample_translator metrics-module)
# not a unit test

add_executable(statsd_tagger_tests statsd_tagger_tests.cpp)
target_link_libraries(statsd_tagger_tests gtest metrics-module)
add_test(statsd_tagger_tests statsd_tagger_tests)

add_executable(statsd_output_writer_fuzz statsd_output_writer_fuzz.cpp)
target_link_libraries(statsd_output_writer_fuzz gmock gtest metrics-module)
# disabled by default
#add_test(statsd_output_writer_fuzz statsd_output_writer_fuzz)

add_executable(statsd_output_writer_tests statsd_output_writer_tests.cpp)
target_link_libraries(statsd_output_writer_tests gtest metrics-module)
add_test(statsd_output_writer_tests statsd_output_writer_tests)

add_executable(sync_util_tests sync_util_tests.cpp)
target_link_libraries(sync_util_tests gtest metrics-module)
add_test(sync_util_tests sync_util_tests)

add_executable(tcp_sender_tests tcp_sender_tests.cpp)
target_link_libraries(tcp_sender_tests gtest metrics-module)
add_test(tcp_sender_tests tcp_sender_tests)

add_executable(udp_sender_tests udp_sender_tests.cpp)
target_link_libraries(udp_sender_tests gtest metrics-module)
add_test(udp_sender_tests udp_sender_tests)