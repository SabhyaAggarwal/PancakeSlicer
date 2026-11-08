#ifndef slic3r_wxinit_hpp_
#define slic3r_wxinit_hpp_

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/html/htmlwin.h>

#undef _

#define __(s)                     wxGetTranslation((s))

#endif
