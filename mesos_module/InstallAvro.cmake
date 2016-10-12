cmake_minimum_required(VERSION 2.8)

include(ExternalProject)

set(AVRO_VERSION 1.8.0)
set(AVRO_TGZ_URL http://apache.mirrors.pair.com/avro/avro-${AVRO_VERSION}/cpp/avro-cpp-${AVRO_VERSION}.tar.gz)
set(AVRO_TGZ_SHA256 ec6e2ec957e95ca07f70cc25f02f5c416f47cb27bd987a6ec770dcbe72527368)

# Download/build avro library and avrogencpp schema generator
ExternalProject_Add(
  ext_avro
  URL ${AVRO_TGZ_URL}
  URL_HASH SHA256=${AVRO_TGZ_SHA256}
  PREFIX ${CMAKE_CURRENT_BINARY_DIR}/ext_avro
  # ensure we trigger 'install', headers are moved around:
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/ext_avro/install
  INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/ext_avro/install)

ExternalProject_Get_Property(ext_avro source_dir binary_dir install_dir)
set(avro_INCLUDE_DIR ${install_dir}/include)
set(avro_LIBRARY ${binary_dir}/libavrocpp.so)
set(avrogencpp_EXECUTABLE ${binary_dir}/avrogencpp)
