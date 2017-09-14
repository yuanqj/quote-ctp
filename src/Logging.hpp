#include <uuid/uuid.h>
#include "spdlog/spdlog.h"

extern const char *MAIN_LOGGER_NAME;
extern std::shared_ptr<spdlog::logger> logger;

void initSpdLog(std::string uuid, std::string fileName);
