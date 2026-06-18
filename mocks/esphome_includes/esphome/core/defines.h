#pragma once
// Compile-time feature flags.
// Most USE_* flags and ESPHOME_* constants are injected via CMake add_compile_definitions().
// This file exists only to satisfy #include "esphome/core/defines.h" and set any
// flags that must be header-defined.

// USE_NETWORK_IPV6 is set per-target in CMakeLists.txt, not globally here.
