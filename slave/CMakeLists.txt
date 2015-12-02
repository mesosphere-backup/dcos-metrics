cmake_minimum_required(VERSION 2.8)

project(stats-slave)

find_path(mesos_SOURCE_DIR PATHS ${CMAKE_SOURCE_DIR}/../../mesos NAMES include/mesos/mesos.hpp)
if(mesos_SOURCE_DIR)
  message(STATUS "Found mesos source dir: ${mesos_SOURCE_DIR}")
else()
  message(WARNING "Didn't find mesos source dir (contains include/mesos/mesos.hpp)")
endif()

find_path(mesos_BUILD_DIR HINTS ${mesos_SOURCE_DIR}/build NAMES include/mesos/mesos.pb.h)
if(mesos_BUILD_DIR)
  message(STATUS "Found mesos build dir: ${mesos_BUILD_DIR}")
else()
  message(WARNING "Didn't find mesos build dir (contains include/mesos/mesos.pb.h)")
endif()

if(NOT mesos_SOURCE_DIR OR NOT mesos_BUILD_DIR)
  message(FATAL_ERROR "Must provide mesos source and build directories.")
endif()

find_library(mesos_LIBRARY NAMES mesos HINTS ${mesos_BUILD_DIR}/src/.libs/)
set(process_SOURCE_DIR ${mesos_SOURCE_DIR}/3rdparty/libprocess)
set(process_BUILD_DIR ${mesos_BUILD_DIR}/3rdparty/libprocess)
find_library(process_LIBRARY NAMES process HINTS ${process_BUILD_DIR}/.libs/)

# Mesos may have linked against the system protobuf, or against its own copy.
# Make sure our includes match or else linking will fail later.
if(USE_MESOS_PROTOBUF)
  include(InstallMesosProtobufIncludes.cmake)
  message(STATUS "Using mesos protobuf: incl:${protobuf_INCLUDE_DIR} lib:${protobuf_LIBRARY}")
else()
  find_path(protobuf_INCLUDE_DIR NAMES google/protobuf/message.h)
  find_library(protobuf_LIBRARY NAMES protobuf)
  message(STATUS "Using system protobuf: incl:${protobuf_INCLUDE_DIR} lib:${protobuf_LIBRARY}")
endif()

find_package(Boost REQUIRED COMPONENTS system)# FIXME: specify required min version?
if(NOT Boost_FOUND)
  message(FATAL_ERROR "Boost System library is required by Boost ASIO, install libboost-system-dev")
endif()
find_path(boost_asio_INCLUDE_DIR NAMES boost/asio.hpp)
if(NOT boost_asio_INCLUDE_DIR)
  message(FATAL_ERROR "Boost ASIO library is required, install libasio-dev")
endif()

find_path(glog_INCLUDE_DIR NAMES glog/logging.h)
if(NOT glog_INCLUDE_DIR)
  message(FATAL_ERROR "Google Log library is required, install libgoogle-glog-dev")
endif()

find_path(linux_prctl_HEADER NAMES sys/prctl.h)
if(linux_prctl_HEADER)
  add_definitions(-DLINUX_PRCTL_AVAILABLE)
endif()

set(LIBS
  pthread
  ${Boost_LIBRARIES}
  ${mesos_LIBRARY}
  ${process_LIBRARY}
  ${protobuf_LIBRARY}
  )
set(SRCS
  env_hook.cpp
  input_assigner.cpp
  input_assigner_factory.cpp
  isolator_module.cpp
  params.cpp
  port_reader.cpp
  port_writer.cpp
  )
configure_file(
  "${PROJECT_SOURCE_DIR}/modules.json.in"
  "${PROJECT_BINARY_DIR}/modules.json"
  )

list(APPEND INCLUDES
  ${Boost_INCLUDE_DIRS}
  ${boost_asio_INCLUDE_DIR}
  ${mesos_SOURCE_DIR}/include
  ${mesos_BUILD_DIR}/include
  ${process_SOURCE_DIR}/include
  ${process_SOURCE_DIR}/3rdparty/stout/include
  ${process_BUILD_DIR}/3rdparty/picojson-1.3.0 # internally required by stout
  ${protobuf_INCLUDE_DIR}
  )

add_definitions(-std=c++11)
include_directories(${INCLUDES})

add_library(stats-slave SHARED ${SRCS})
target_link_libraries(stats-slave ${LIBS})

set(TESTS_ENABLED true)
if(TESTS_ENABLED)
  message(STATUS "Unit tests enabled.")
  enable_testing()
  add_subdirectory(tests)
else()
  message(STATUS "Unit tests disabled.")
endif()
