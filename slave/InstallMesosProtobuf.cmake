cmake_minimum_required(VERSION 2.8)

include(ExternalProject)

set(PROTOBUF_VERSION 2.5.0)
set(PROTOBUF_TGZ_FILENAME protobuf-${PROTOBUF_VERSION}.tar.gz)
find_file(PROTOBUF_TGZ NAMES ${PROTOBUF_TGZ_FILENAME} PATHS ${process_SOURCE_DIR}/3rdparty)
if(NOT PROTOBUF_TGZ)
  # not found locally, fall back to web
  set(PROTOBUF_TGZ https://github.com/3rdparty/mesos-3rdparty/raw/master/${PROTOBUF_TGZ_FILENAME})
endif()

# Download/build protobuf (TODO reuse mesos' build if present)
ExternalProject_Add(
  ext_protobuf
  URL ${PROTOBUF_TGZ}
  PREFIX ${CMAKE_CURRENT_BINARY_DIR}/protobuf
  CONFIGURE_COMMAND ${CMAKE_CURRENT_BINARY_DIR}/protobuf/src/ext_protobuf/configure
  BUILD_COMMAND make
  INSTALL_COMMAND ""
  )

add_library(protobuf_LIBRARY IMPORTED STATIC GLOBAL)
add_dependencies(protobuf_LIBRARY ext_protobuf)
ExternalProject_Get_Property(ext_protobuf source_dir binary_dir)
set(protobuf_SOURCE_DIR ${source_dir})
set(protobuf_BUILD_DIR ${binary_dir})
set(protobuf_INCLUDE_DIR ${PROTOBUF_SOURCE_DIR}/include)
set_target_properties(protobuf_LIBRARY PROPERTIES
  "IMPORTED_LOCATION" "${protobuf_BUILD_DIR}/src/.libs/libprotobuf.so"
  "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}"
  )