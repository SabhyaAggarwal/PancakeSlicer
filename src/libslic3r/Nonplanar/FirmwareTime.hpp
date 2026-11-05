#pragma once
#include "libslic3r/Point.hpp"
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace Slic3r {
class PrintConfig;
namespace Nonplanar {

struct FirmwareTimedMove {
    Vec3d from, to;
    double seconds = 0.;
    size_t line = 0;
    int object = -1, patch = -1;
    bool extruding = false;
};
struct FirmwareTime {
    std::vector<FirmwareTimedMove> moves;
    double seconds = 0.;
    bool valid = false;
};
bool firmware_time_supported(const PrintConfig &config, bool bambu);
FirmwareTime firmware_time(const std::string &file, const PrintConfig &config, bool bambu,
                           const std::function<void()> &cancel);

}
}
