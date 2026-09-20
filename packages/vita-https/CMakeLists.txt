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
  project(vita_https VERSION 1.0.0 LANGUAGES C)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(VITA_HTTPS_BUILD_EXAMPLE "Compile the GET example" OFF)

set(VITA_HTTPS_CURL_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/curl-mbedtls"
  CACHE PATH "Pinned Vita libcurl installation built with Mbed TLS")
if(NOT EXISTS "${VITA_HTTPS_CURL_ROOT}/include/curl/curl.h" OR
   NOT EXISTS "${VITA_HTTPS_CURL_ROOT}/lib/libcurl.a")
  message(FATAL_ERROR
    "The hardened curl/Mbed TLS transport was not found at "
    "${VITA_HTTPS_CURL_ROOT}. Run tools/build-curl-mbedtls.sh first. "
    "The VitaSDK OpenSSL build is intentionally not accepted.")
endif()

if(NOT DEFINED ENV{VITASDK})
  message(FATAL_ERROR "VITASDK is required to resolve the pinned Mbed TLS port")
endif()
set(VITA_HTTPS_MBEDTLS_VERSION_HEADER
  "$ENV{VITASDK}/arm-vita-eabi/include/mbedtls/build_info.h")
if(NOT EXISTS "${VITA_HTTPS_MBEDTLS_VERSION_HEADER}")
  message(FATAL_ERROR "Mbed TLS headers are missing from VITASDK")
endif()
file(READ "${VITA_HTTPS_MBEDTLS_VERSION_HEADER}" VITA_HTTPS_MBEDTLS_BUILD_INFO)
if(NOT VITA_HTTPS_MBEDTLS_BUILD_INFO MATCHES
   "MBEDTLS_VERSION_NUMBER[ \t]+0x03[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]")
  message(FATAL_ERROR
    "vita-https requires Mbed TLS 3.x. Reinstall the current VitaSDK mbedtls package.")
endif()
set(VITA_HTTPS_CURL_PROVENANCE
  "${VITA_HTTPS_CURL_ROOT}/share/BUILD-PROVENANCE.txt")
if(NOT EXISTS "${VITA_HTTPS_CURL_PROVENANCE}")
  message(FATAL_ERROR "Missing curl build provenance; rebuild with tools/build-curl-mbedtls.sh")
endif()
file(READ "${VITA_HTTPS_CURL_PROVENANCE}" VITA_HTTPS_CURL_PROVENANCE_TEXT)
if(NOT VITA_HTTPS_CURL_PROVENANCE_TEXT MATCHES "curl.tls_backend=Mbed TLS")
  message(FATAL_ERROR "The supplied libcurl was not certified as an Mbed TLS build")
endif()

add_library(vita_https STATIC
  src/vita_https.c
  src/net_runtime.c
  src/ca_bundle.c)
add_library(VitaHttps::VitaHttps ALIAS vita_https)
set_target_properties(vita_https PROPERTIES EXPORT_NAME VitaHttps)
target_include_directories(vita_https
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${VITA_HTTPS_CURL_ROOT}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_directories(vita_https PUBLIC
  $<BUILD_INTERFACE:${VITA_HTTPS_CURL_ROOT}/lib>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_LIBDIR}/vita-https>)
target_compile_features(vita_https PUBLIC c_std_11)
target_compile_options(vita_https PRIVATE
  -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard
  -O2 -ffunction-sections -fdata-sections)
target_link_libraries(vita_https PUBLIC
  curl mbedtls mbedx509 mbedcrypto zstd z pthread
  SceNet_stub SceNetCtl_stub SceSysmodule_stub)

if(VITA_HTTPS_BUILD_EXAMPLE)
  add_library(vita_https_get_example OBJECT examples/get.c)
  target_link_libraries(vita_https_get_example PRIVATE VitaHttps::VitaHttps)
endif()

install(TARGETS vita_https EXPORT VitaHttpsTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/vita_https.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES "${VITA_HTTPS_CURL_ROOT}/lib/libcurl.a"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/vita-https)
install(EXPORT VitaHttpsTargets NAMESPACE VitaHttps::
  FILE VitaHttpsTargets.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VitaHttps)
configure_package_config_file(cmake/VitaHttpsConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/VitaHttpsConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VitaHttps)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/VitaHttpsConfigVersion.cmake
  VERSION 1.0.0 COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/VitaHttpsConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/VitaHttpsConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VitaHttps)
install(FILES LICENSE THIRD_PARTY_NOTICES.md DEPENDENCIES.lock
  DESTINATION share/licenses/vita-https)
install(FILES licenses/certifi-MPL-2.0.txt CA_BUNDLE_PROVENANCE.md
  DESTINATION share/licenses/vita-https)
install(FILES sources/certifi-2026.07.22-cacert.pem
  DESTINATION share/sources/vita-https)
install(DIRECTORY "${VITA_HTTPS_CURL_ROOT}/share/licenses/"
  DESTINATION share/licenses/vita-https/third-party)
