cmake_minimum_required(VERSION 2.8)

include(ExternalProject)

set(PICOJSON_VERSION 1.3.0)
set(PICOJSON_TGZ_FILENAME picojson-${PICOJSON_VERSION}.tar.gz)
find_file(PICOJSON_TGZ
    NAMES ${PICOJSON_TGZ_FILENAME}
    PATHS
      ${process_INCLUDE_DIR}/../3rdparty
      ${mesos_INCLUDE_DIR}/../3rdparty/libprocess/3rdparty
      ${mesos_INCLUDE_DIR}/../3rdparty)
if(NOT PICOJSON_TGZ)
  # not found locally, fall back to web
  message(STATUS "Didn't find Mesos ${PICOJSON_TGZ_FILENAME}, will download")
  set(PICOJSON_TGZ https://github.com/3rdparty/mesos-3rdparty/raw/master/${PICOJSON_TGZ_FILENAME})
else()
  message(STATUS "Found Mesos picojson: ${PICOJSON_TGZ}")
endif()

# Download/build picojson (FIXME reuse mesos' build if present?)
ExternalProject_Add(
  ext_picojson
  URL ${PICOJSON_TGZ}
  PREFIX ${CMAKE_CURRENT_BINARY_DIR}/ext_picojson
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
  )

ExternalProject_Get_Property(ext_picojson source_dir binary_dir)
set(picojson_INCLUDE_DIR ${source_dir})
