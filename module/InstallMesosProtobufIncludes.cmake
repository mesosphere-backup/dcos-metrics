cmake_minimum_required(VERSION 2.8)

include(ExternalProject)

set(PROTOBUF_VERSION 2.5.0)
set(PROTOBUF_TGZ_FILENAME protobuf-${PROTOBUF_VERSION}.tar.gz)
find_file(PROTOBUF_TGZ
    NAMES ${PROTOBUF_TGZ_FILENAME}
    PATHS
      ${process_INCLUDE_DIR}/../3rdparty
      ${mesos_INCLUDE_DIR}/../3rdparty/libprocess/3rdparty)
if(NOT PROTOBUF_TGZ)
  # not found locally, fall back to web
  message(STATUS "Didn't find Mesos ${PROTOBUF_TGZ_FILENAME}, will download")
  set(PROTOBUF_TGZ https://github.com/3rdparty/mesos-3rdparty/raw/master/${PROTOBUF_TGZ_FILENAME})
else()
  message(STATUS "Found Mesos protobuf: ${PROTOBUF_TGZ}")
endif()

# Download/build protobuf (FIXME reuse mesos' build if present?)
ExternalProject_Add(
  ext_protobuf
  URL ${PROTOBUF_TGZ}
  PREFIX ${CMAKE_CURRENT_BINARY_DIR}/ext_protobuf
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
  )

ExternalProject_Get_Property(ext_protobuf source_dir binary_dir)
set(protobuf_INCLUDE_DIR ${source_dir}/src)
