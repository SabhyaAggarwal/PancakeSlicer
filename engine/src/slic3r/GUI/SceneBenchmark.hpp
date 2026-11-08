#pragma once

#include <vector>

namespace Slic3r {
namespace GUI {

struct FrameTimeStats
{
    double fps{ 0.0 };
    double average_ms{ 0.0 };
    double median_ms{ 0.0 };
    double p95_ms{ 0.0 };
    double p99_ms{ 0.0 };
    double max_ms{ 0.0 };
};

FrameTimeStats frame_time_stats(std::vector<double> frame_ms);

void run_scene_benchmark();

}
}
