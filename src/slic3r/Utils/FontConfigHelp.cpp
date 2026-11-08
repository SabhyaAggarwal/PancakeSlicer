#include "FontConfigHelp.hpp"
#include <optional>
#include <string>
#include <wx/string.h>
#include <wx/buffer.h>
#include <wx/chartype.h>
#include <wx/font.h>
#include <cstddef>

#ifdef EXIST_FONT_CONFIG_INCLUDE

#include <wx/filename.h>
#include <fontconfig/fontconfig.h>
#include "libslic3r/Utils.hpp"


static std::optional<Slic3r::ScopeGuard> finalize_guard;
static FcConfig *fc = nullptr;

std::string Slic3r::GUI::get_font_path(const wxFont &font, bool reload_fonts)
{
    if (!finalize_guard.has_value()) {
        FcInit();
        fc = FcInitLoadConfigAndFonts();
        finalize_guard.emplace([]() {
            FcConfigDestroy(fc);
        });
    } else if (reload_fonts) {
        FcConfigDestroy(fc);
        fc = FcInitLoadConfigAndFonts();
    }

    if (fc == nullptr) return "";

    wxString                 fontDesc = font.GetNativeFontInfoUserDesc();
    wxString                 faceName = font.GetFaceName();
    const wxScopedCharBuffer faceNameBuffer = faceName.ToUTF8();
    const char *             fontFamily     = faceNameBuffer;

    int slant = FC_SLANT_ROMAN;
    if (fontDesc.Find(wxS("Oblique")) != wxNOT_FOUND)
        slant = FC_SLANT_OBLIQUE;
    else if (fontDesc.Find(wxS("Italic")) != wxNOT_FOUND)
        slant = FC_SLANT_ITALIC;

    int weight = FC_WEIGHT_NORMAL;
    if (fontDesc.Find(wxS("Book")) != wxNOT_FOUND)
        weight = FC_WEIGHT_BOOK;
    else if (fontDesc.Find(wxS("Medium")) != wxNOT_FOUND)
        weight = FC_WEIGHT_MEDIUM;
#ifdef FC_WEIGHT_ULTRALIGHT
    else if (fontDesc.Find(wxS("Ultra-Light")) != wxNOT_FOUND)
        weight = FC_WEIGHT_ULTRALIGHT;
#endif
    else if (fontDesc.Find(wxS("Light")) != wxNOT_FOUND)
        weight = FC_WEIGHT_LIGHT;
    else if (fontDesc.Find(wxS("Semi-Bold")) != wxNOT_FOUND)
        weight = FC_WEIGHT_DEMIBOLD;
#ifdef FC_WEIGHT_ULTRABOLD
    else if (fontDesc.Find(wxS("Ultra-Bold")) != wxNOT_FOUND)
        weight = FC_WEIGHT_ULTRABOLD;
#endif
    else if (fontDesc.Find(wxS("Bold")) != wxNOT_FOUND)
        weight = FC_WEIGHT_BOLD;
    else if (fontDesc.Find(wxS("Heavy")) != wxNOT_FOUND)
        weight = FC_WEIGHT_BLACK;

    int width = FC_WIDTH_NORMAL;
    if (fontDesc.Find(wxS("Ultra-Condensed")) != wxNOT_FOUND)
        width = FC_WIDTH_ULTRACONDENSED;
    else if (fontDesc.Find(wxS("Extra-Condensed")) != wxNOT_FOUND)
        width = FC_WIDTH_EXTRACONDENSED;
    else if (fontDesc.Find(wxS("Semi-Condensed")) != wxNOT_FOUND)
        width = FC_WIDTH_SEMICONDENSED;
    else if (fontDesc.Find(wxS("Condensed")) != wxNOT_FOUND)
        width = FC_WIDTH_CONDENSED;
    else if (fontDesc.Find(wxS("Ultra-Expanded")) != wxNOT_FOUND)
        width = FC_WIDTH_ULTRAEXPANDED;
    else if (fontDesc.Find(wxS("Extra-Expanded")) != wxNOT_FOUND)
        width = FC_WIDTH_EXTRAEXPANDED;
    else if (fontDesc.Find(wxS("Semi-Expanded")) != wxNOT_FOUND)
        width = FC_WIDTH_SEMIEXPANDED;
    else if (fontDesc.Find(wxS("Expanded")) != wxNOT_FOUND)
        width = FC_WIDTH_EXPANDED;

    FcResult   res;
    FcPattern *matchPattern = FcPatternBuild(NULL, FC_FAMILY, FcTypeString,
                                             (FcChar8 *) fontFamily, NULL);
    ScopeGuard sg_mp([matchPattern]() { FcPatternDestroy(matchPattern); });

    FcPatternAddInteger(matchPattern, FC_SLANT, slant);
    FcPatternAddInteger(matchPattern, FC_WEIGHT, weight);
    FcPatternAddInteger(matchPattern, FC_WIDTH, width);

    FcConfigSubstitute(NULL, matchPattern, FcMatchPattern);
    FcDefaultSubstitute(matchPattern);

    FcPattern *resultPattern = FcFontMatch(NULL, matchPattern, &res);
    if (resultPattern == nullptr) return "";
    ScopeGuard sg_rp([resultPattern]() { FcPatternDestroy(resultPattern); });

    FcChar8 *fileName;
    if (FcPatternGetString(resultPattern, FC_FILE, 0, &fileName) !=
        FcResultMatch)
        return "";
    wxString fontFileName = wxString::FromUTF8((char *) fileName);

    if (fontFileName.IsEmpty()) return "";

    wxFileName myFileName(fontFileName);
    if (!myFileName.IsOk()) return "";

    if (myFileName.IsRelative()) {
        if (!(myFileName.MakeAbsolute() && myFileName.FileExists())) {
            return "";
        }
    }

    if (!myFileName.FileExists() || !myFileName.IsFileReadable()) return "";

    wxString fullFileName = myFileName.GetFullPath();
    return std::string(fullFileName.c_str());
}

#endif