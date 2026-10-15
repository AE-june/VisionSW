#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <string>

namespace vision {

inline void LoggerInit() {
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("vision",
        spdlog::sinks_init_list{ consoleSink });

    logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::debug);
}

} // namespace vision

#define VISION_LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)
#define VISION_LOG_INFO(...)  spdlog::info(__VA_ARGS__)
#define VISION_LOG_WARN(...)  spdlog::warn(__VA_ARGS__)
#define VISION_LOG_ERROR(...) spdlog::error(__VA_ARGS__)
