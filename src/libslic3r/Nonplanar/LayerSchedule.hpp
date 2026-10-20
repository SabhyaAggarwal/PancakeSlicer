#pragma once

#include <vector>

namespace Slic3r { class PrintObject; }

namespace Slic3r::Nonplanar {

struct LayerScheduleParams
{
    double height;
    double fine;
    double stair;
    double min_height;
};

bool field_layers(const PrintObject &object);
bool field_layers_considered(const PrintObject &object);

LayerScheduleParams layer_schedule_params(const PrintObject &object);
double field_layer_slope_max();

std::vector<double> field_layer_profile(const PrintObject &object);

}
