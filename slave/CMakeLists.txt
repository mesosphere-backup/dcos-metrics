cmake_minimum_required(VERSION 2.6)

project(stats-slave)

find_path(mesos_SOURCE_DIR PATHS /home/nick/code/mesos NAMES include/mesos/mesos.hpp)
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
find_library(process_LIBRARY NAMES process HINTS ${mesos_BUILD_DIR}/3rdparty/libprocess/.libs/)
find_library(protobuf_LIBRARY NAMES protobuf)

find_package(Boost REQUIRED COMPONENTS system)# FIXME: specify required min version?
if(NOT Boost_FOUND)
  message(FATAL_ERROR "Boost System library is required by Boost ASIO, install libboost-system-dev")
endif()
find_path(boost_asio_INCLUDE_DIR NAMES boost/asio.hpp)
if(NOT boost_asio_INCLUDE_DIR)
  message(FATAL_ERROR "Boost ASIO library is required, install libasio-dev")
endif()

find_path(linux_prctl_HEADER NAMES sys/prctl.h)
if(linux_prctl_HEADER)
  add_definitions(-DLINUX_PRCTL_AVAILABLE)
endif()

set(LIBS
  ${Boost_LIBRARIES}
  ${mesos_LIBRARY}
  ${process_LIBRARY}
  ${protobuf_LIBRARY}
  )
set(SRCS
  env_hook.cpp
  input_assigner.cpp
  input_assigner_impl.cpp
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
  ${mesos_SOURCE_DIR}/3rdparty/libprocess/include
  ${mesos_SOURCE_DIR}/3rdparty/libprocess/3rdparty/stout/include
  ${mesos_BUILD_DIR}/include
  ${mesos_BUILD_DIR}/3rdparty/libprocess/3rdparty/picojson-1.3.0 # internally required by stout
  )

add_definitions(-std=c++11)
include_directories(${INCLUDES})

add_library(stats-slave SHARED ${SRCS})
target_link_libraries(stats-slave ${LIBS})

find_package(GTest)
if(NOT GTEST_FOUND)
  message(STATUS "Didn't find GTest on default system paths, trying /usr/src/gtest")
  set(GTEST_ROOT /usr/src/gtest)
  find_package(GTest)
endif()
if(GTEST_FOUND)
  message(STATUS "Found GTest. Unit tests enabled.")
  enable_testing()
  #add_directory(tests) TODO
else()
  message(WARNING "GTest not found. Unit tests disabled. To enable them, first install libgtest-dev and build /usr/src/gtest")
endif()
