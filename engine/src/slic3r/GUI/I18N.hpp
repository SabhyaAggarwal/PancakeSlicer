#include <wx/string.h>
#include <wx/translation.h>
#include <wx/strconv.h>
#include <string>
#ifndef _
#define _(s)    	Slic3r::GUI::I18N::translate((s))
#define _L(s)    	Slic3r::GUI::I18N::translate((s))
#define _devL(s)	wxString((s))
#define _omitL(s)   ("")
#define _utf8(s)    Slic3r::GUI::I18N::translate_utf8((s))
#define _u8L(s)     Slic3r::GUI::I18N::translate_utf8((s))
#endif

#ifndef _L_CONTEXT
#define _L_CONTEXT(s, ctx) 	 Slic3r::GUI::I18N::translate((s), (ctx))
#define _u8L_CONTEXT(s, ctx) Slic3r::GUI::I18N::translate_utf8((s), (ctx))
#endif

#ifndef L
#define L(s) s
#endif

#ifndef L_CONTEXT
#define L_CONTEXT(s, context) s
#endif

#ifndef _CHB
#define _CHB(s) wxGetTranslation(wxString(s, wxConvUTF8)).utf8_str()
#endif

#ifndef slic3r_GUI_I18N_hpp_
#define slic3r_GUI_I18N_hpp_

#include <wx/intl.h>
#include <wx/version.h>

namespace Slic3r { namespace GUI { 

namespace I18N {
	inline wxString translate(const char         *s) { return wxGetTranslation(wxString(s, wxConvUTF8)); }
	inline wxString translate(const wchar_t      *s) { return wxGetTranslation(s); }
	inline wxString translate(const std::string  &s) { return wxGetTranslation(wxString(s.c_str(), wxConvUTF8)); }
	inline wxString translate(const std::wstring &s) { return wxGetTranslation(s.c_str()); }
	inline wxString translate(const wxString     &s) { return wxGetTranslation(s); }

	inline wxString translate(const char         *s, const char 	    *plural, unsigned int n) { return wxGetTranslation(wxString(s, wxConvUTF8), wxString(plural, wxConvUTF8), n); }
	inline wxString translate(const wchar_t      *s, const wchar_t	    *plural, unsigned int n) { return wxGetTranslation(s, plural, n); }
	inline wxString translate(const std::string  &s, const std::string  &plural, unsigned int n) { return wxGetTranslation(wxString(s.c_str(), wxConvUTF8), wxString(plural.c_str(), wxConvUTF8), n); }
	inline wxString translate(const std::wstring &s, const std::wstring &plural, unsigned int n) { return wxGetTranslation(s.c_str(), plural.c_str(), n); }
	inline wxString translate(const wxString     &s, const wxString     &plural, unsigned int n) { return wxGetTranslation(s, plural, n); }

	inline std::string translate_utf8(const char         *s) { return wxGetTranslation(wxString(s, wxConvUTF8)).ToUTF8().data(); }
	inline std::string translate_utf8(const wchar_t      *s) { return wxGetTranslation(s).ToUTF8().data(); }
	inline std::string translate_utf8(const std::string  &s) { return wxGetTranslation(wxString(s.c_str(), wxConvUTF8)).ToUTF8().data(); }
	inline std::string translate_utf8(const std::wstring &s) { return wxGetTranslation(s.c_str()).ToUTF8().data(); }
	inline std::string translate_utf8(const wxString     &s) { return wxGetTranslation(s).ToUTF8().data(); }

	inline std::string translate_utf8(const char         *s, const char 	    *plural, unsigned int n) { return translate(s, plural, n).ToUTF8().data(); }
	inline std::string translate_utf8(const wchar_t      *s, const wchar_t	    *plural, unsigned int n) { return translate(s, plural, n).ToUTF8().data(); }
	inline std::string translate_utf8(const std::string  &s, const std::string  &plural, unsigned int n) { return translate(s, plural, n).ToUTF8().data(); }
	inline std::string translate_utf8(const std::wstring &s, const std::wstring &plural, unsigned int n) { return translate(s, plural, n).ToUTF8().data(); }
	inline std::string translate_utf8(const wxString     &s, const wxString     &plural, unsigned int n) { return translate(s, plural, n).ToUTF8().data(); }

#define _wxGetTranslation_ctx(S, CTX) wxGetTranslation((S), wxEmptyString, (CTX))

	inline wxString translate(const char *s, const char* ctx)         { return _wxGetTranslation_ctx(wxString(s, wxConvUTF8), ctx); }
	inline wxString translate(const wchar_t *s, const char* ctx)      { return _wxGetTranslation_ctx(s, ctx); }
	inline wxString translate(const std::string &s, const char* ctx)  { return _wxGetTranslation_ctx(wxString(s.c_str(), wxConvUTF8), ctx); }
	inline wxString translate(const std::wstring &s, const char* ctx) { return _wxGetTranslation_ctx(s.c_str(), ctx); }
	inline wxString translate(const wxString &s, const char* ctx)     { return _wxGetTranslation_ctx(s, ctx); }

	inline std::string translate_utf8(const char *s, const char* ctx)         { return _wxGetTranslation_ctx(wxString(s, wxConvUTF8), ctx).ToUTF8().data(); }
	inline std::string translate_utf8(const wchar_t *s, const char* ctx)      { return _wxGetTranslation_ctx(s, ctx).ToUTF8().data(); }
	inline std::string translate_utf8(const std::string &s, const char* ctx)  { return _wxGetTranslation_ctx(wxString(s.c_str(), wxConvUTF8), ctx).ToUTF8().data(); }
	inline std::string translate_utf8(const std::wstring &s, const char* ctx) { return _wxGetTranslation_ctx(s.c_str(), ctx).ToUTF8().data(); }
	inline std::string translate_utf8(const wxString &s, const char* ctx)     { return _wxGetTranslation_ctx(s, ctx).ToUTF8().data(); }

#undef _wxGetTranslation_ctx
}
wxString	L_str(const std::string &str);

}
}
#ifndef _L_PLURAL
#define _L_PLURAL(s, plural, n) Slic3r::GUI::I18N::translate(s, plural, n)
#endif

#endif
