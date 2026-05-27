# CMake generated Testfile for 
# Source directory: /home/mc/dev/esphome-coap-server-tests
# Build directory: /home/mc/dev/esphome-coap-server-tests/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[test_cbor_encode]=] "/home/mc/dev/esphome-coap-server-tests/build/test_cbor_encode")
set_tests_properties([=[test_cbor_encode]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/mc/dev/esphome-coap-server-tests/CMakeLists.txt;104;add_test;/home/mc/dev/esphome-coap-server-tests/CMakeLists.txt;107;add_coap_test;/home/mc/dev/esphome-coap-server-tests/CMakeLists.txt;0;")
add_test([=[test_cbor_device_info]=] "/home/mc/dev/esphome-coap-server-tests/build/test_cbor_device_info")
set_tests_properties([=[test_cbor_device_info]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/mc/dev/esphome-coap-server-tests/CMakeLists.txt;104;add_test;/home/mc/dev/esphome-coap-server-tests/CMakeLists.txt;108;add_coap_test;/home/mc/dev/esphome-coap-server-tests/CMakeLists.txt;0;")
add_test([=[test_oscore_crypto]=] "/home/mc/dev/esphome-coap-server-tests/build/test_oscore_crypto")
set_tests_properties([=[test_oscore_crypto]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/mc/dev/esphome-coap-server-tests/CMakeLists.txt;104;add_test;/home/mc/dev/esphome-coap-server-tests/CMakeLists.txt;109;add_coap_test;/home/mc/dev/esphome-coap-server-tests/CMakeLists.txt;0;")
add_test([=[test_oscore_keys]=] "/home/mc/dev/esphome-coap-server-tests/build/test_oscore_keys")
set_tests_properties([=[test_oscore_keys]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/mc/dev/esphome-coap-server-tests/CMakeLists.txt;104;add_test;/home/mc/dev/esphome-coap-server-tests/CMakeLists.txt;110;add_coap_test;/home/mc/dev/esphome-coap-server-tests/CMakeLists.txt;0;")
subdirs("_deps/googletest-build")
