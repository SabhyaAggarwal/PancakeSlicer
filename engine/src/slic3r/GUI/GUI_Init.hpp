#ifndef slic3r_GUI_Init_hpp_
#define slic3r_GUI_Init_hpp_

#include <libslic3r/Preset.hpp>
#include <libslic3r/PrintConfig.hpp>
#include <vector>
#include <utility>
#include <string>

namespace Slic3r {

namespace GUI {

struct OpenGLVersions
{
    static const std::vector<std::pair<int, int>> core;
};

struct GUI_InitParams
{
	int		                    argc;
	char	                  **argv;

	PresetsConfigSubstitutions  preset_substitutions;

    std::vector<std::string>    load_configs;
    DynamicPrintConfig          extra_config;
    std::vector<std::string>    input_files;

    std::pair<int, int>         opengl_version                  { 0, 0 };
    bool                        opengl_debug                    { false };
    bool                        opengl_compatibility_profile    { false };

	bool                        input_gcode { false };
};

int GUI_Run(GUI_InitParams &params);

}
}

#endif
