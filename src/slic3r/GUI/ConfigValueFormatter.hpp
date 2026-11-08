#pragma once

#include <string>

#include <wx/string.h>

namespace Slic3r {

class DynamicPrintConfig;

namespace GUI {

wxString get_string_value(const std::string& opt_key, const DynamicPrintConfig& config);

wxString get_full_label(const std::string& opt_key, const DynamicPrintConfig& config);

std::string get_pure_opt_key(const std::string& opt_key);

wxString get_string_from_enum(const std::string& opt_key, const DynamicPrintConfig& config, bool is_infill = false, int idx = -1);

}
}
