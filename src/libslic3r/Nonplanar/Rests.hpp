#pragma once

namespace Slic3r { class PrintObject; }

namespace Slic3r::Nonplanar {

struct ConformalResult;

struct Floating
{
    double part = 0., support = 0.;
};

Floating floating_road(const PrintObject &object, const ConformalResult *result);

Floating planar_control_floating(const PrintObject &object, bool support = true);

bool floats_past_bar(const Floating &print, const Floating &control);

}
