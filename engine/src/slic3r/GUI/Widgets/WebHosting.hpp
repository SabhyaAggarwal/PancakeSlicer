#pragma once

#include <wx/string.h>

namespace Slic3r { namespace GUI { namespace web_hosting {

constexpr const char* BOOTSTRAP_PAGE = "web/dialog/WebDialog/blank.html";

wxString bootstrap_url();

wxString content_base_url();

bool is_content_url(const wxString& url);

const char* orca_bridge_script();

}}}
