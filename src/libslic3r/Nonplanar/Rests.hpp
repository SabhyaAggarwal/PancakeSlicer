#pragma once

#include <string>



namespace Slic3r { class PrintObject; struct GCodeProcessorResult; }

namespace Slic3r::Nonplanar {

struct ConformalResult;



struct Floating
{
    double part = 0., support = 0.;



    double air = 0.;
};






Floating floating_road(const PrintObject &object, const ConformalResult *result);




Floating floating_road(const GCodeProcessorResult &result, const PrintObject &object);






Floating planar_control_floating(const PrintObject &object, bool support = true);



bool floats_past_bar(const Floating &print, const Floating &control);




std::string may_need_support(const std::string &name, const Floating &planar);

}
