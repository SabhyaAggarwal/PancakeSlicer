#ifndef slic3r_GUI_format_hpp_
#define slic3r_GUI_format_hpp_

#include <libslic3r/format.hpp>

#include <string>
#include <ostream>
#include <wx/string.h>
#include <wx/buffer.h>

namespace Slic3r { 
namespace GUI { 

template<typename... TArgs>
inline wxString format_wxstr(const char* fmt, TArgs&&... args) {
	boost::format message(fmt);
	return wxString::FromUTF8(Slic3r::internal::format::format_recursive(message, std::forward<TArgs>(args)...).c_str());
}
template<typename... TArgs>
inline wxString format_wxstr(const std::string& fmt, TArgs&&... args) {
	boost::format message(fmt);
	return wxString::FromUTF8(Slic3r::internal::format::format_recursive(message, std::forward<TArgs>(args)...).c_str());
}
template<typename... TArgs>
inline wxString format_wxstr(const wxString& fmt, TArgs&&... args) {
	return format_wxstr(fmt.ToUTF8().data(), std::forward<TArgs>(args)...);
}
template<typename... TArgs>
inline std::string format(const char* fmt, TArgs&&... args) {
    return Slic3r::format(fmt, std::forward<TArgs>(args)...);
}
template<typename... TArgs>
inline std::string format(const std::string& fmt, TArgs&&... args) {
    return Slic3r::format(fmt, std::forward<TArgs>(args)...);
}
template<typename... TArgs>
inline std::string format(const wxString& fmt, TArgs&&... args) {
    return Slic3r::format(fmt.ToUTF8().data(), std::forward<TArgs>(args)...);
}

}

namespace internal {
	namespace format {
		struct utf8_buffer { 
			wxScopedCharBuffer data;
		};
		inline const utf8_buffer cook(const wxString &arg) {
			return utf8_buffer { arg.ToUTF8() };
		}
		inline const utf8_buffer cook(wxString &arg) {
			return utf8_buffer { arg.ToUTF8() };
		}
		inline const utf8_buffer cook(wxString &&arg) {
			return utf8_buffer{ arg.ToUTF8() };
		}
	}
}

}

namespace boost {
	namespace io {
		namespace detail {
			inline std::ostream& operator<<(std::ostream& os, const Slic3r::internal::format::utf8_buffer& str) {
				os << str.data.data();
				return os;
			}
		}
	}
}

#endif
