cmake_minimum_required(VERSION 2.8)

project(stats-slave)

# Version is just used in order to find the directory named "mesos-VERSION"
if(mesos_VERSION)
  message(STATUS "Mesos version: ${mesos_VERSION}")
else()
  message(FATAL_ERROR "Mesos version required. Run with eg 'cmake -Dmesos_VERSION=0.26.0 ..'")
endif()

message(STATUS "Searching for Mesos at ${CMAKE_SOURCE_DIR}/../mesos-${mesos_VERSION}...")
find_path(mesos_SOURCE_DIR NAMES include/mesos/mesos.hpp PATHS ${CMAKE_SOURCE_DIR}/../mesos-${mesos_VERSION} NO_DEFAULT_PATH)
if(mesos_SOURCE_DIR)
  message(STATUS "Found Mesos source dir: ${mesos_SOURCE_DIR}")
else()
  message(WARNING "Didn't find Mesos source dir for v${mesos_VERSION} (contains include/mesos/mesos.hpp).")
endif()

find_path(mesos_BUILD_DIR NAMES include/mesos/mesos.pb.h HINTS ${mesos_SOURCE_DIR}/build NO_DEFAULT_PATH)
if(mesos_BUILD_DIR)
  message(STATUS "Found Mesos build dir: ${mesos_BUILD_DIR}")
else()
  message(WARNING "Didn't find Mesos build dir for v${mesos_VERSION} (contains include/mesos/mesos.pb.h).")
endif()

if(NOT mesos_SOURCE_DIR OR NOT mesos_BUILD_DIR)
  message(FATAL_ERROR "Unable to proceed without Mesos source/build directories.")
endif()

find_library(mesos_LIBRARY NAMES mesos HINTS ${mesos_BUILD_DIR}/src/.libs/)
find_path(process_SOURCE_DIR NAMES include/process/process.hpp HINTS ${mesos_SOURCE_DIR}/3rdparty/libprocess NO_DEFAULT_PATH)
find_path(stout_SOURCE_DIR NAMES include/stout/stringify.hpp HINTS ${process_SOURCE_DIR}/3rdparty/stout NO_DEFAULT_PATH)

# Dragged in by stout, so we have to find it:
option(USE_MESOS_PICOJSON "Whether to use Mesos' internal picojson (true) or system picojson (false)" TRUE)
if(USE_MESOS_PICOJSON)
  include(InstallMesosPicojsonIncludes.cmake)
  message(STATUS "Using Mesos picojson: incl:${picojson_INCLUDE_DIR}")
else()
  find_path(picojson_INCLUDE_DIR NAMES picojson.h NO_DEFAULT_PATH)
  message(STATUS "Using system protobuf: incl:${protobuf_INCLUDE_DIR}")
endif()

# Mesos may have linked against the system protobuf, or against its own copy.
# Make sure our includes match or else linking will fail later.
option(USE_MESOS_PROTOBUF "Whether to use Mesos' internal protobuf (true) or system protobuf (false)" TRUE)
if(USE_MESOS_PROTOBUF)
  include(InstallMesosProtobufIncludes.cmake)
  message(STATUS "Using Mesos protobuf: incl:${protobuf_INCLUDE_DIR} lib:<static within libmesos>")
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
  )
if(NOT USE_MESOS_PROTOBUF)
  list(APPEND LIBS ${protobuf_LIBRARY})
endif()
set(SRCS
  env_hook.cpp
  input_assigner.cpp
  input_assigner_factory.cpp
  isolator_module.cpp
  params.cpp
  port_reader_impl.cpp
  port_runner_impl.cpp
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
  ${stout_SOURCE_DIR}/include
  ${picojson_INCLUDE_DIR} # internally required by stout
  ${protobuf_INCLUDE_DIR}
  )

add_definitions(-std=c++11)
include_directories(${INCLUDES})

add_library(stats-slave SHARED ${SRCS})
target_link_libraries(stats-slave ${LIBS})
# ensure header dependencies are unpacked before we start building
if(USE_MESOS_PROTOBUF)
  add_dependencies(stats-slave ext_protobuf)
endif()
if(USE_MESOS_PICOJSON)
  add_dependencies(stats-slave ext_picojson)
endif()

set(TESTS_ENABLED true)
if(TESTS_ENABLED)
  message(STATUS "Unit tests enabled.")
  enable_testing()
  add_subdirectory(tests)
else()
  message(STATUS "Unit tests disabled.")
endif()
