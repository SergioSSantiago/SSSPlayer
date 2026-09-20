cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VITASDK})
      set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake"
          CACHE PATH "VitaSDK toolchain")
    else()
      message(FATAL_ERROR "VITASDK is required")
    endif()
  endif()
  project(vita_hw_decoder VERSION 2.0.0 LANGUAGES C)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(VITA_HW_DECODER_FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/ffmpeg-vita-hw"
    CACHE PATH "Pinned FFmpeg h264_vita installation")
option(VITA_HW_DECODER_BUILD_EXAMPLE "Compile the local-file example" OFF)

if(NOT EXISTS "${VITA_HW_DECODER_FFMPEG_ROOT}/include/libavcodec/avcodec.h")
  message(FATAL_ERROR
    "FFmpeg h264_vita was not found at ${VITA_HW_DECODER_FFMPEG_ROOT}. "
    "Run tools/build-ffmpeg.sh or set VITA_HW_DECODER_FFMPEG_ROOT.")
endif()

add_library(vita_hw_decoder STATIC
  src/vita_hw_decoder.c
  src/file_stream.c
  src/module_log.c
  src/decoder_runtime.c
  src/internal/software_video.c
  src/internal/elementary_audio.c
  src/internal/vita_aac_decoder.c)
add_library(VitaHwDecoder::VitaHwDecoder ALIAS vita_hw_decoder)
set_target_properties(vita_hw_decoder PROPERTIES
  EXPORT_NAME VitaHwDecoder
  VERSION 2.0.0
  SOVERSION 2)

target_include_directories(vita_hw_decoder
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${VITA_HW_DECODER_FFMPEG_ROOT}/include)
target_link_directories(vita_hw_decoder PUBLIC
  $<BUILD_INTERFACE:${VITA_HW_DECODER_FFMPEG_ROOT}/lib>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_LIBDIR}/vita-hw-decoder>)
target_link_libraries(vita_hw_decoder PUBLIC
  avformat avcodec avutil vita2d
  SceVideodec_stub SceCodecEngine_stub SceAudiodec_stub SceGxm_stub
  SceAppMgr_stub SceCommonDialog_stub SceAudio_stub SceDisplay_stub
  pthread m)
target_compile_features(vita_hw_decoder PUBLIC c_std_11)
target_compile_definitions(vita_hw_decoder PRIVATE
  VITA_HW_DECODER_HAS_H264_VITA=1
  VITA_HW_DECODER_ALLOW_SOFTWARE_FALLBACK=0)
target_compile_options(vita_hw_decoder PRIVATE
  -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard
  -O3 -ffunction-sections -fdata-sections)

if(VITA_HW_DECODER_BUILD_EXAMPLE)
  add_library(vita_hw_decoder_local_example OBJECT examples/local_file.c)
  target_link_libraries(vita_hw_decoder_local_example PRIVATE VitaHwDecoder::VitaHwDecoder)
endif()

install(TARGETS vita_hw_decoder EXPORT VitaHwDecoderTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/vita_hw_decoder.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES
  "${VITA_HW_DECODER_FFMPEG_ROOT}/lib/libavformat.a"
  "${VITA_HW_DECODER_FFMPEG_ROOT}/lib/libavcodec.a"
  "${VITA_HW_DECODER_FFMPEG_ROOT}/lib/libavutil.a"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/vita-hw-decoder)
install(EXPORT VitaHwDecoderTargets NAMESPACE VitaHwDecoder::
  FILE VitaHwDecoderTargets.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VitaHwDecoder)
configure_package_config_file(cmake/VitaHwDecoderConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/VitaHwDecoderConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VitaHwDecoder)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/VitaHwDecoderConfigVersion.cmake
  VERSION 2.0.0 COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/VitaHwDecoderConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/VitaHwDecoderConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VitaHwDecoder)
install(FILES LICENSE THIRD_PARTY_NOTICES.md DEPENDENCIES.lock
  DESTINATION share/licenses/vita-hw-decoder)
install(DIRECTORY "${VITA_HW_DECODER_FFMPEG_ROOT}/share/licenses/"
  DESTINATION share/licenses/vita-hw-decoder/third-party)
install(DIRECTORY "${VITA_HW_DECODER_FFMPEG_ROOT}/share/sources/"
  DESTINATION share/sources/vita-hw-decoder/third-party)
