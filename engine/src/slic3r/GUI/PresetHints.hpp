#ifndef slic3r_PresetHints_hpp_
#define slic3r_PresetHints_hpp_

#include <string>

namespace Slic3r { class Preset; }
namespace Slic3r { class PresetBundle; }


namespace Slic3r {

class PresetHints
{
public:
    static std::string cooling_description(const Preset &preset);
    
    static std::string maximum_volumetric_flow_description(const PresetBundle &preset_bundle);

    static std::string recommended_thin_wall_thickness(const PresetBundle &preset_bundle);

    static std::string top_bottom_shell_thickness_explanation(const PresetBundle &preset_bundle);    
};

}

#endif
