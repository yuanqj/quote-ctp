#include "Logging.hpp"

const char *MAIN_LOGGER_NAME = "main";
std::shared_ptr<spdlog::logger> logger = spdlog::get(MAIN_LOGGER_NAME);

void initSpdLog(std::string uuid, std::string fileName) {
    spdlog::set_async_mode(1024, spdlog::async_overflow_policy::block_retry, nullptr, std::chrono::seconds(5));
    logger = spdlog::rotating_logger_mt(MAIN_LOGGER_NAME, fileName, 1024*1024*1024, 100);
    logger->set_pattern("%Y-%m-%dT%H:%M:%S.%f%z %l uuid=" + uuid + ", pid=%P, tid=%t | %v");
}
