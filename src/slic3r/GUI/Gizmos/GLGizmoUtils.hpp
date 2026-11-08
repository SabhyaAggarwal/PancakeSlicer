#ifndef GL_GIZMO_UTIL_HPP
#define GL_GIZMO_UTIL_HPP

#include <map>
#include <utility>
#include <vector>
#include <string>
#include <wx/string.h>
#include "imgui.h"

namespace Slic3r::GUI {

class ImGuiWrapper;
class GLCanvas3D;

namespace GLGizmoUtils {

	void render_tooltip_button(
		ImGuiWrapper* imgui_wrapper, const GLCanvas3D& canvas, const std::vector<std::pair<wxString, wxString>>& shortcuts, float x, float y);

    void render_wiki_buttons(
        ImGuiWrapper* imgui_wrapper, const GLCanvas3D& canvas, const char*  wiki_url, const char* video_url = "");
    bool render_wiki_guide_button(const GLCanvas3D& canvas, float scale, const char* url = "");
    bool render_video_guide_button(const GLCanvas3D& canvas, float scale, const char* url = "");
	
	void begin_right_aligned_buttons(const std::vector<wxString>& labels);

	void push_orca_button_style();

	void pop_orca_button_style();

    bool toolbar_circular_button(ImTextureID textureID, const char* id, float scale);

}
}

#endif