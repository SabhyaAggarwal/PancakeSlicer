#pragma once

#include <string>

namespace Slic3r {

class AppConfig;

namespace instance_id {

std::string ensure(AppConfig& config);

void reset_cache_for_tests();

}

}
