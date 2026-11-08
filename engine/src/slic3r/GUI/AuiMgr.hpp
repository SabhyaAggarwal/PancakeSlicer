#pragma once

#include <wx/aui/framemanager.h>

namespace Slic3r { namespace GUI {

class AuiMgr : public wxAuiManager
{
public:
    void init(wxWindow* window);
    void apply_color_mode();
    void track_docked_size(wxWindow* window);

    static wxAuiPaneInfo sidebar_pane_info();

    wxAuiFloatingFrame* CreateFloatingFrame(wxWindow* parent, const wxAuiPaneInfo& pane) override;
};

}}
