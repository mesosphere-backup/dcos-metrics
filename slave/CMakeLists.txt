cmake_minimum_required(VERSION 2.8)

set(CMAKE_BUILD_TYPE "Release")

#cmake
# -Dboost_system_LIBRARY=/path/to/matching/libboost_system.so
# -DTARGET_DCOS_16
# -Dmesos_VERSION=0.27.1 ..
option(TARGET_DCOS_16 "Whether to build with compatibility for DCOS 1.6")
if(TARGET_DCOS_16)
  set(CMAKE_CXX_COMPILER "/usr/bin/g++-4.8")
  set(CMAKE_CXX_FLAGS "-std=c++11")
endif()

project(stats-slave)

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
find_path(stout_INCLUDE_DIR NAMES stout/stringify.hpp HINTS ${process_INCLUDE_DIR}/../3rdparty/stout/include ${mesos_INCLUDE_DIR})

# We don't use this, but it's dragged in via stout's includes, so we're forced to find it so that stout doesn't break the build:
option(USE_LOCAL_PICOJSON "Whether to use Mesos' or a locally downloaded internal build of picojson (true) or system picojson (false)" TRUE)
if(USE_LOCAL_PICOJSON AND NOT picojson_INCLUDE_DIR)
  include(InstallMesosPicojsonIncludes.cmake)
  message(STATUS "Using Mesos or locally-downloaded picojson: incl:${picojson_INCLUDE_DIR}")
else()
  find_path(picojson_INCLUDE_DIR NAMES picojson.h HINTS ${mesos_INCLUDE_DIR})
  message(STATUS "Using system picojson: incl:${picojson_INCLUDE_DIR}")
endif()

# Mesos may have linked against the system protobuf, or against its own copy.
# Make sure our includes match Mesos or else linking will fail later.
option(USE_LOCAL_PROTOBUF "Whether to use Mesos' or a locally downloaded internal build of protobuf (true) or system protobuf (false)" TRUE)
if(USE_LOCAL_PROTOBUF AND NOT protobuf_INCLUDE_DIR)
  include(InstallMesosProtobufIncludes.cmake)
  message(STATUS "Using Mesos or locally-downloaded protobuf: incl:${protobuf_INCLUDE_DIR} lib:<static within libmesos>")
else()
  find_path(protobuf_INCLUDE_DIR NAMES google/protobuf/message.h HINTS ${mesos_INCLUDE_DIR})
  find_library(protobuf_LIBRARY NAMES protobuf HINTS ${default_LIB_DIR})
  message(STATUS "Using system protobuf: incl:${protobuf_INCLUDE_DIR} lib:${protobuf_LIBRARY}")
endif()

find_library(boost_system_LIBRARY NAMES boost_system HINTS ${default_LIB_DIR})
if(NOT boost_system_LIBRARY)
  message(FATAL_ERROR "Boost System library is required by Boost ASIO.\napt-get: Install 'libboost-system-dev'\nyum: install 'boost-system'\n...or run cmake with eg 'cmake -Dboost_system_LIBRARY=/opt/mesosphere/lib/libboost_system.so ..'")
endif()

find_path(boost_asio_INCLUDE_DIR NAMES boost/asio.hpp HINTS ${mesos_INCLUDE_DIR})
if(NOT boost_asio_INCLUDE_DIR)
  message(FATAL_ERROR "Boost ASIO library is required.\napt-get: Install 'libasio-dev'\nyum: Install '???'\n...or run cmake with eg 'cmake -Dboost_asio_INCLUDE_DIR=/opt/mesosphere/include ..' (contains boost/asio.hpp)")
endif()

option(USE_LOCAL_GLOG "Whether to use Mesos' copy of glog (true) or the system copy of glog (false)" TRUE)
if(USE_LOCAL_GLOG)
  find_path(glog_INCLUDE_DIR NAMES glog/logging.h HINTS ${mesos_INCLUDE_DIR})
  if(NOT glog_INCLUDE_DIR)
    message(FATAL_ERROR "Google Log library is required.\napt-get: Install 'libgoogle-glog-dev'\nyum: Install '???'\n...or run cmake with eg 'cmake -Dglog_INCLUDE_DIR=/opt/mesosphere/include ..' (contains glog/logging.h)")
  endif()
  # don't link glog explicitly -- libmesos includes it
else()
  find_path(glog_INCLUDE_DIR NAMES glog/logging.h)
  if(NOT glog_INCLUDE_DIR)
    message(FATAL_ERROR "Google Log library is required.\napt-get: Install 'libgoogle-glog-dev'\nyum: Install '???'\n...or run cmake with eg 'cmake -Dglog_INCLUDE_DIR=/opt/mesosphere/include ..' (contains glog/logging.h)")
  endif()
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
  ${boost_system_LIBRARY}
  ${mesos_LIBRARY}
  )
if(glog_LIRARY)
  list(APPEND LIBS ${glog_LIBRARY})
endif()
if(protobuf_LIBRARY)
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
  tag_datadog.cpp
  tag_key_prefix.cpp
  )
configure_file(
  "${PROJECT_SOURCE_DIR}/modules.json.in"
  "${PROJECT_BINARY_DIR}/modules.json"
  )

list(APPEND INCLUDES
  ${boost_asio_INCLUDE_DIR}
  ${mesos_INCLUDE_DIR}
  ${mesos_PROTOBUF_INCLUDE_DIR}
  ${process_INCLUDE_DIR}
  ${stout_INCLUDE_DIR}
  ${picojson_INCLUDE_DIR} # internally required by stout
  ${protobuf_INCLUDE_DIR}
  )

message(STATUS "Include dirs: ${INCLUDES}")
message(STATUS "Libs: ${LIBS}")

add_definitions(-std=c++11)
include_directories(${INCLUDES})

add_library(stats-slave SHARED ${SRCS})
target_link_libraries(stats-slave ${LIBS})
# ensure header dependencies are unpacked before we start building
if(USE_LOCAL_PROTOBUF)
  add_dependencies(stats-slave ext_protobuf)
endif()
if(USE_LOCAL_PICOJSON)
  add_dependencies(stats-slave ext_picojson)
endif()

option(TESTS_ENABLED "Whether to build unit tests." true)
if(TESTS_ENABLED)
  message(STATUS "Unit tests enabled.")
  enable_testing()
  add_subdirectory(tests)
else()
  message(STATUS "Unit tests disabled.")
endif()

install(TARGETS stats-slave
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib/static)
