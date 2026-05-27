#include "esphome/core/application.h"
#include "esphome/components/logger/logger.h"

namespace esphome {
Application App;
}

namespace esphome::logger {
Logger *global_logger = nullptr;
}
