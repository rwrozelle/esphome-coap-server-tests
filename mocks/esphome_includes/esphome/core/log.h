#pragma once
#include <cstdio>
#include <cinttypes>

// Route all ESP_LOG* macros to stdout so test output is visible.
#define ESP_LOGV(tag, fmt, ...) (void)(tag)
#define ESP_LOGD(tag, fmt, ...) (void)(tag)
#define ESP_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", (tag), ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", (tag), ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", (tag), ##__VA_ARGS__)
#define ESP_LOGCONFIG(tag, fmt, ...) printf("[C][%s] " fmt "\n", (tag), ##__VA_ARGS__)

#define ESP_LOG_VERBOSE 5
#define ESP_LOG_DEBUG   4
#define ESP_LOG_INFO    3
#define ESP_LOG_WARN    2
#define ESP_LOG_ERROR   1
#define ESP_LOG_BUFFER_HEX_LEVEL(tag, buf, len, level) (void)(tag)

// LogString type used by mark_failed(const LogString *)
struct LogString {
  const char *str;
};
#define LOG_STR(s) ([] { static const LogString ls{s}; return &ls; }())
