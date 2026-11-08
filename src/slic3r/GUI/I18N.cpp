#include "I18N.hpp"
#include <wx/string.h>
#include <string>
#include <wx/translation.h>
#include <wx/strconv.h>

namespace Slic3r { namespace GUI { 

wxString L_str(const std::string &str)
{
	return wxGetTranslation(wxString(str.c_str(), wxConvUTF8));
}

} }
