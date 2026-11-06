#pragma once








#include "LayerField.hpp"
#include "libslic3r/ExPolygon.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Slic3r {
class PrintObject;
}

namespace Slic3r::Nonplanar {

struct ConformalResult;


bool native_requested();




double native_axis_key(const char *name, double rotary_default, bool rotary);




bool generate_planar_native_support(PrintObject &object);



std::shared_ptr<const LayerField> native_field(PrintObject &object);


void build_native_patch(PrintObject &object, const std::shared_ptr<const LayerField> &field, ConformalResult &result);



class NativeSliceOverride
{
public:
    NativeSliceOverride(std::string name, std::vector<ExPolygons> slices);
    ~NativeSliceOverride();
    NativeSliceOverride(const NativeSliceOverride &) = delete;
    NativeSliceOverride &operator=(const NativeSliceOverride &) = delete;

    static const std::vector<ExPolygons> *find(const std::string &name);

private:
    std::string m_name;
};

}
