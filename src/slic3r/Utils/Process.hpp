#ifndef GUI_PROCESS_HPP
#define GUI_PROCESS_HPP

#include <vector>


class wxWindow;
class wxString;

namespace Slic3r {
namespace GUI {

void start_new_slicer(const wxString *path_to_open = nullptr, bool single_instance = false);
void start_new_slicer(const std::vector<wxString>& files, bool single_instance = false);

void start_new_gcodeviewer(const wxString *path_to_open = nullptr);
void start_new_gcodeviewer_open_file(wxWindow *parent = nullptr);

}
}

#endif
