#ifndef slic3r_GUI_PostProcessor_hpp_
#define slic3r_GUI_PostProcessor_hpp_

#include <functional>
#include <string>

namespace Slic3r { class DynamicPrintConfig; }


namespace Slic3r {

extern bool run_post_process_scripts(
    std::string& src_path, bool make_copy, const std::string& host, std::string& output_name, const DynamicPrintConfig& config);

inline bool run_post_process_scripts(std::string& src_path, const DynamicPrintConfig& config)
{
    std::string src_path_name = src_path;
    return run_post_process_scripts(src_path, false, "File", src_path_name, config);
}

extern void gcode_add_line_number(const std::string& path, const DynamicPrintConfig& config);
}

#endif
