cmake_minimum_required(VERSION 3.20)
project(esphome_coap_server_tests CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Path to ESPHome source tree (override with -DESPHOME_SRC=...)
# ---------------------------------------------------------------------------
set(ESPHOME_SRC "/home/mc/dev/esphome" CACHE PATH "Path to ESPHome root")
set(COAP_SERVER_SRC "${ESPHOME_SRC}/esphome/components/coap_server")

# ---------------------------------------------------------------------------
# Google Test
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# OpenSSL (backs the PSA Crypto mock)
# ---------------------------------------------------------------------------
find_package(OpenSSL REQUIRED)

# ---------------------------------------------------------------------------
# Compile-time feature flags — mirror what a real coap_server build uses
# ---------------------------------------------------------------------------
add_compile_definitions(
  USE_OPENTHREAD
  USE_COAP_OSCORE
  USE_SENSOR
  USE_SWITCH
  USE_BINARY_SENSOR
  USE_TEXT_SENSOR
  USE_NUMBER
  USE_LOCK
  USE_VALVE
  USE_BUTTON
  USE_LOGGER
  USE_COAP_SERVER_PORT=5683
  USE_COAP_SERVER_MAX_CLIENTS=4
  ESPHOME_BOARD="test"
  ESPHOME_VERSION="test"
)

# ---------------------------------------------------------------------------
# Mock library — link-seam stubs that replace IDF/OT/PSA APIs
# ---------------------------------------------------------------------------
add_library(coap_server_mocks STATIC
  mocks/mock_openthread.cpp
  mocks/mock_nvs.cpp
  mocks/mock_psa.cpp
  mocks/mock_app.cpp
)

target_include_directories(coap_server_mocks PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/mocks     # cbor.h, nvs.h, openthread/, psa/
  ${CMAKE_CURRENT_SOURCE_DIR}/mocks/esphome_includes  # esphome/core/*, esphome/components/*
  ${ESPHOME_SRC}                        # so #include "esphome/..." resolves normally
)

target_link_libraries(coap_server_mocks PUBLIC OpenSSL::Crypto gmock)

# Per-directory include override: mocks/esphome_includes shadows ${ESPHOME_SRC}/esphome/...
# for headers that we want to replace entirely (defines.h, application.h, helpers.h, etc.)
# Headers not present in mocks/esphome_includes fall through to the real ESPHome source.

# ---------------------------------------------------------------------------
# coap_server source library
# ---------------------------------------------------------------------------
add_library(coap_server_lib STATIC
  ${COAP_SERVER_SRC}/coap_server_cbor.cpp
  ${COAP_SERVER_SRC}/coap_server_ot.cpp
)

target_include_directories(coap_server_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/mocks
  ${CMAKE_CURRENT_SOURCE_DIR}/mocks/esphome_includes
  ${ESPHOME_SRC}
)

target_link_libraries(coap_server_lib PUBLIC coap_server_mocks)

# ---------------------------------------------------------------------------
# Test executables
# ---------------------------------------------------------------------------
enable_testing()

function(add_coap_test name src)
  add_executable(${name} ${src})
  target_include_directories(${name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/esphome_includes
    ${ESPHOME_SRC}
  )
  # Strip 'final' so tests can subclass CoapServer to access protected members.
  target_compile_definitions(${name} PRIVATE "final=")
  target_link_libraries(${name} PRIVATE coap_server_lib coap_server_mocks gtest gtest_main gmock)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_coap_test(test_cbor_encode       tests/test_cbor_encode.cpp)
add_coap_test(test_cbor_device_info  tests/test_cbor_device_info.cpp)
add_coap_test(test_oscore_crypto     tests/test_oscore_crypto.cpp)
add_coap_test(test_oscore_keys       tests/test_oscore_keys.cpp)
