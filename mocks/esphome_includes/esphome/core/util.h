#pragma once

namespace esphome {
namespace setup_priority {
static constexpr float AFTER_WIFI = -10.0f;
static constexpr float WIFI = -50.0f;
static constexpr float BEFORE_CONNECTION = -100.0f;
static constexpr float HARDWARE = 100.0f;
}  // namespace setup_priority
}  // namespace esphome
