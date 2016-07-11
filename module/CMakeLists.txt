cmake_minimum_required(VERSION 2.8)

option(TARGET_DCOS "Version of DCOS to configure for")
option(TESTS_ENABLED "Whether to build unit tests." TRUE)
option(USE_LOCAL_PICOJSON "Whether to use Mesos' or a locally downloaded internal build of picojson (true) or system picojson (false)" TRUE)
option(USE_LOCAL_PROTOBUF "Whether to use Mesos' or a locally downloaded internal build of protobuf (true) or system protobuf (false)" TRUE)
option(USE_LOCAL_GLOG "Whether to use Mesos' copy of glog (true) or the system copy of glog (false)" TRUE)
option(USE_LOCAL_AVRO "Whether to download/build a local copy of avrocpp (true) or to use the system copy of avrocpp (false)" TRUE)

if(TARGET_DCOS EQUAL "1.8")
  #cmake -DCMAKE_BUILD_TYPE=Release -Dboost_system_LIBRARY=/home/nick/code/boost_1_53_0/stage/lib/libboost_system.so.1.53.0 -DUSE_LOCAL_AVRO=False -Davro_LIBRARY=/usr/local/lib/libavrocpp.so.1.8.0.0 -DTARGET_DCOS=1.8 ..
  set(CMAKE_CXX_COMPILER "/usr/bin/g++-4.8")
  set(mesos_VERSION "1.0.0")
  set(PROTOBUF_VERSION 2.6.1)
  set(TESTS_ENABLED false)
endif()

if(TARGET_DCOS EQUAL "1.7")
  #cmake -DCMAKE_BUILD_TYPE=Release -Dboost_system_LIBRARY=/home/nick/code/boost_1_53_0/stage/lib/libboost_system.so.1.53.0 -DUSE_LOCAL_AVRO=False -Davro_LIBRARY=/usr/local/lib/libavrocpp.so.1.8.0.0 -DTARGET_DCOS=1.7 ..
  set(CMAKE_CXX_COMPILER "/usr/bin/g++-4.8")
  set(mesos_VERSION "0.28.1")
  set(PROTOBUF_VERSION 2.5.0)
  set(TESTS_ENABLED false)
endif()

project(metrics-module)

# Search for mesos includes (contains mesos/mesos.hpp) and mesos protobufs (contains mesos/mesos.pb.h).

if(mesos_VERSION)

  # Use mesos_VERSION to find a directory named "mesos-VERSION" which was created via get-mesos.sh helper script.
  message(STATUS "Searching for Mesos ${mesos_VERSION} include dir created by get-mesos.sh at ${CMAKE_SOURCE_DIR}/../mesos-${mesos_VERSION}")
  find_path(mesos_INCLUDE_DIR NAMES mesos/mesos.hpp PATHS ${CMAKE_SOURCE_DIR}/../mesos-${mesos_VERSION}/include NO_DEFAULT_PATH)
  if(NOT mesos_INCLUDE_DIR)
    message(FATAL_ERROR "Didn't find Mesos include dir for v${mesos_VERSION} (should contain mesos/mesos.hpp). Did you run 'get-mesos.sh ${mesos_VERSION}' yet?")
  endif()
  find_path(mesos_PROTOBUF_INCLUDE_DIR mesos/mesos.pb.h PATHS ${CMAKE_SOURCE_DIR}/../mesos-${mesos_VERSION}/build/include NO_DEFAULT_PATH)
  if(NOT mesos_INCLUDE_DIR)
    message(FATAL_ERROR "Didn't find Mesos protobuf dir for v${mesos_VERSION} (should contain mesos/mesos.pb.h). Did you run 'get-mesos.sh ${mesos_VERSION}' yet?")
  endif()

elseif(mesos_INCLUDE_DIR)

  # Use mesos_INCLUDE_DIR for manually pointing to a copy of mesos which was built without get-mesos.sh.

  message(STATUS "Using Mesos include dir: ${mesos_INCLUDE_DIR}")
  if(mesos_PROTOBUF_INCLUDE_DIR)
    message(STATUS "Using Mesos protobuf dir: ${mesos_PROTOBUF_INCLUDE_DIR}")
  else()
    # Set mesos_PROTOBUF_INCLUDE_DIR to match mesos_INCLUDE_DIR by default.
    find_path(mesos_PROTOBUF_INCLUDE_DIR NAMES mesos/mesos.pb.h PATHS ${mesos_INCLUDE_DIR} NO_DEFAULT_PATH)
    if(NOT mesos_PROTOBUF_INCLUDE_DIR)
      message(FATAL_ERROR "Mesos protobufs not found in mesos_INCLUDE_DIR (should contain mesos/mesos.pb.h).\nSpecify protobuf dir manually with eg 'cmake -Dmesos_PROTOBUF_INCLUDE_DIR=/opt/mesosphere/include ..'")
    endif()
  endif()

else()

  # Fall back to using whatever's on the system

  message(STATUS "Searching on system for Mesos include dir containing mesos/mesos.hpp")
  find_path(mesos_INCLUDE_DIR NAMES mesos/mesos.hpp)
  if(NOT mesos_INCLUDE_DIR)
    message(FATAL_ERROR "Mesos include dir not found on system (should contain mesos/mesos.hpp).\nOne of 'mesos_VERSION' or 'mesos_INCLUDE_DIR' is required. Run cmake with:\neg 'cmake -Dmesos_VERSION=0.26.0 ..' (for Mesos built with get-mesos.sh)\nor 'cmake -Dmesos_INCLUDE_DIR=/opt/mesosphere/include ..' (for Mesos built without get-mesos.sh)")
  endif()
  message(STATUS "Searching on system for Mesos protobuf dir containing mesos/mesos.pb.h")
  find_path(mesos_PROTOBUF_INCLUDE_DIR NAMES mesos/mesos.pb.h)
  if(NOT mesos_PROTOBUF_INCLUDE_DIR)
    message(FATAL_ERROR "Mesos protobuf dir not found on system (should contain mesos/mesos.pb.h).\nOne of 'mesos_VERSION' or 'mesos_INCLUDE_DIR' is required. Run cmake with:\neg 'cmake -Dmesos_VERSION=0.26.0 ..' (for Mesos built with get-mesos.sh)\nor 'cmake -Dmesos_INCLUDE_DIR=/opt/mesosphere/include ..' (for Mesos built without get-mesos.sh)")
  endif()

endif()

# Search for libmesos.so

if(mesos_VERSION)

  # Use lib which was created via get-mesos.sh helper script.

  message(STATUS "Searching for libmesos.so created by get-mesos.sh at ${CMAKE_SOURCE_DIR}/../mesos-${mesos_VERSION}/build/src/.libs/")
  find_library(mesos_LIBRARY NAMES mesos HINTS ${CMAKE_SOURCE_DIR}/../mesos-${mesos_VERSION}/build/src/.libs/ NO_DEFAULT_PATH)
  if(NOT mesos_LIBRARY)
    message(FATAL_ERROR "Unable to find libmesos.so in Mesos v${mesos_VERSION} build dir: ${CMAKE_SOURCE_DIR}/../mesos-${mesos_VERSION}/build/src/.libs/")
  endif()

else()

  # Use whatever's installed on system

  message(STATUS "Searching on system for libmesos.so")
  find_library(mesos_LIBRARY NAMES mesos HINTS ${default_LIB_DIR})
  if(NOT mesos_LIBRARY)
    message(FATAL_ERROR "libmesos.so not found on system.\nOne of 'mesos_VERSION' or 'mesos_LIBRARY' is required. Run cmake with:\neg 'cmake -Dmesos_VERSION=0.26.0 ..' (for Mesos built with get-mesos.sh)\nor 'cmake -Dmesos_LIBRARY=/opt/mesosphere/lib/libmesos.so ..' (for Mesos built without get-mesos.sh)")
  endif()

endif()

# Search for libprocess and stout, hopefully within Mesos includes but with fallback to system.
find_path(process_INCLUDE_DIR NAMES process/process.hpp HINTS ${mesos_INCLUDE_DIR}/../3rdparty/libprocess/include ${mesos_INCLUDE_DIR})
find_path(stout_INCLUDE_DIR NAMES stout/stringify.hpp
  HINTS
  ${mesos_INCLUDE_DIR}/../3rdparty/stout/include # >= 1.0
  ${process_INCLUDE_DIR}/../3rdparty/stout/include # <= 0.28
  ${mesos_INCLUDE_DIR})

# We don't use this, but it's dragged in via stout's includes, so we're forced to find it so that stout doesn't break the build:
if(USE_LOCAL_PICOJSON AND NOT picojson_INCLUDE_DIR)
  include(InstallMesosPicojsonIncludes.cmake)
  message(STATUS "Using Mesos or locally-downloaded picojson: incl:${picojson_INCLUDE_DIR}")
  set(PICOJSON_DEPENDS ext_picojson)
else()
  find_path(picojson_INCLUDE_DIR NAMES picojson.h HINTS ${mesos_INCLUDE_DIR})
  message(STATUS "Using system picojson: incl:${picojson_INCLUDE_DIR}")
endif()

# Mesos may have linked against the system protobuf, or against its own copy.
# Make sure our includes match Mesos or else linking will fail later.
if(USE_LOCAL_PROTOBUF AND NOT protobuf_INCLUDE_DIR)
  include(InstallMesosProtobufIncludes.cmake)
  message(STATUS "Using Mesos or locally-downloaded protobuf: incl:${protobuf_INCLUDE_DIR} lib:<static within libmesos>")
  set(PROTOBUF_DEPENDS ext_protobuf)
else()
  find_path(protobuf_INCLUDE_DIR NAMES google/protobuf/message.h HINTS ${mesos_INCLUDE_DIR})
  find_library(protobuf_LIBRARY NAMES protobuf HINTS ${default_LIB_DIR})
  message(STATUS "Using system protobuf: incl:${protobuf_INCLUDE_DIR} lib:${protobuf_LIBRARY}")
endif()

if(USE_LOCAL_AVRO)
  include(InstallAvro.cmake)
  set(AVRO_DEPENDS ext_avro)
else()
  find_path(avro_INCLUDE_DIR NAMES avro/Compiler.hh HINTS ${mesos_INCLUDE_DIR})
  find_library(avro_LIBRARY NAMES avrocpp HINTS ${default_LIB_DIR})
  find_program(avrogencpp_EXECUTABLE avrogencpp HINTS ${default_BIN_DIR})
endif()

find_library(boost_system_LIBRARY NAMES boost_system HINTS ${default_LIB_DIR})
if(NOT boost_system_LIBRARY)
  message(FATAL_ERROR "Boost System library is required by Boost ASIO.\napt-get: Install 'libboost-system-dev'\nyum: install 'boost-system'\n...or run cmake with eg 'cmake -Dboost_system_LIBRARY=/opt/mesosphere/lib/libboost_system.so ..'")
endif()

find_path(boost_asio_INCLUDE_DIR NAMES boost/asio.hpp HINTS ${mesos_INCLUDE_DIR})
if(NOT boost_asio_INCLUDE_DIR)
  message(FATAL_ERROR "Boost ASIO library is required.\napt-get: Install 'libasio-dev'\nyum: Install '???'\n...or run cmake with eg 'cmake -Dboost_asio_INCLUDE_DIR=/opt/mesosphere/include ..' (contains boost/asio.hpp)")
endif()

find_path(glog_INCLUDE_DIR NAMES glog/logging.h HINTS ${mesos_INCLUDE_DIR})
if(NOT glog_INCLUDE_DIR)
  message(FATAL_ERROR "Google Log library is required.\napt-get: Install 'libgoogle-glog-dev'\nyum: Install '???'\n...or run cmake with eg 'cmake -Dglog_INCLUDE_DIR=/opt/mesosphere/include ..' (contains glog/logging.h)")
endif()
if(NOT USE_LOCAL_GLOG)
  find_library(glog_LIBRARY NAMES glog HINTS ${default_LIB_DIR})
  if(NOT glog_LIBRARY)
    message(STATUS "Google GLog *may* be needed to complete the build, depending on your Mesos build.\napt-get: Install 'libgoogle-glog-dev'\nyum: install '???'\n...or run cmake with eg 'cmake -Dglog_LIBRARY=/opt/mesosphere/lib/libglog.so ..'")
  endif()
endif()

find_path(linux_prctl_HEADER NAMES sys/prctl.h)
if(linux_prctl_HEADER)
  add_definitions(-DLINUX_PRCTL_AVAILABLE)
endif()

set(LIBS
  pthread
  ${avro_LIBRARY}
  ${boost_system_LIBRARY}
  ${mesos_LIBRARY})
if(glog_LIBRARY)
  list(APPEND LIBS ${glog_LIBRARY})
endif()
if(protobuf_LIBRARY)
  list(APPEND LIBS ${protobuf_LIBRARY})
endif()

set(SRCS
  avro_encoder.cpp
  collector_output_writer.cpp
  container_assigner.cpp
  container_assigner_strategy.cpp
  container_reader_impl.cpp
  container_state_cache_impl.cpp
  io_runner_impl.cpp
  isolator_module.cpp
  memnmem.cpp
  module_access_factory.cpp
  params.cpp
  resource_estimator_module.cpp
  sync_util.cpp
  statsd_output_writer.cpp
  statsd_tagger.cpp
  statsd_util.cpp
  tcp_sender.cpp
  udp_sender.cpp)
configure_file(
  "${PROJECT_SOURCE_DIR}/modules.json.in"
  "${PROJECT_BINARY_DIR}/modules.json")

list(APPEND EXTERN_INCLUDES
  ${boost_asio_INCLUDE_DIR}
  ${mesos_INCLUDE_DIR}
  ${mesos_PROTOBUF_INCLUDE_DIR}
  ${process_INCLUDE_DIR}
  ${stout_INCLUDE_DIR}
  ${avro_INCLUDE_DIR}
  ${picojson_INCLUDE_DIR} # internally required by stout
  ${protobuf_INCLUDE_DIR})

message(STATUS "Include dirs: ${EXTERN_INCLUDES}")
message(STATUS "Libs: ${LIBS}")

add_definitions(-DPICOJSON_USE_INT64) # otherwise we get "undefined symbol: _ZNK8picojson5value2isIlEEbv"

if(CMAKE_CXX_COMPILER_ID STREQUAL GNU OR CMAKE_CXX_COMPILER_ID STREQUAL Clang)
  set(CMAKE_CXX_FLAGS "-std=c++11 -Wall -Wextra")
endif()
include_directories(${CMAKE_BINARY_DIR}) # for metrics schema
# for library headers, hide any warnings:
include_directories(SYSTEM ${EXTERN_INCLUDES})

add_library(metrics-module SHARED ${SRCS})
target_link_libraries(metrics-module ${LIBS})
# ensure any header dependencies are unpacked before we start building
add_dependencies(metrics-module ${AVRO_DEPENDS} ${PROTOBUF_DEPENDS} ${PICOJSON_DEPENDS})

if(TESTS_ENABLED)
  message(STATUS "Unit tests enabled.")
  enable_testing()
  add_subdirectory(tests)
else()
  message(STATUS "Unit tests disabled.")
endif()

# generate avro schema structs:
set(AVRO_SCHEMA_INPUT ${CMAKE_SOURCE_DIR}/../schema/metrics.avsc)
set(AVRO_SCHEMA_STRUCT_OUTPUT ${CMAKE_BINARY_DIR}/metrics_schema_struct.hpp)
add_custom_command(
  OUTPUT ${AVRO_SCHEMA_STRUCT_OUTPUT}
  COMMAND LD_LIBRARY_PATH=${default_LIB_DIR} ${avrogencpp_EXECUTABLE} -i ${AVRO_SCHEMA_INPUT} -n metrics_schema -o ${AVRO_SCHEMA_STRUCT_OUTPUT}
  DEPENDS ${AVRO_DEPENDS}
  COMMENT "Generating metrics schema Struct header")
add_custom_target(generate-avro-structs
  DEPENDS ${AVRO_SCHEMA_STRUCT_OUTPUT} ${AVRO_SCHEMA_INPUT})
add_dependencies(metrics-module generate-avro-structs)
# generate avro schema json:
set(AVRO_SCHEMA_JSON_OUTPUT ${CMAKE_BINARY_DIR}/metrics_schema_json.hpp)
add_custom_command(
  OUTPUT ${AVRO_SCHEMA_JSON_OUTPUT}
  COMMAND python ${CMAKE_SOURCE_DIR}/generate_metrics_schema_json.py ${AVRO_SCHEMA_INPUT} ${AVRO_SCHEMA_JSON_OUTPUT}
  DEPENDS ${AVRO_DEPENDS}
  COMMENT "Generating metrics schema JSON header")
add_custom_target(generate-avro-json
  DEPENDS ${AVRO_SCHEMA_JSON_OUTPUT} ${AVRO_SCHEMA_INPUT})
add_dependencies(metrics-module generate-avro-json)

install(TARGETS metrics-module
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib/static)
