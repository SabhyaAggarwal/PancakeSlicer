#ifndef slic3r_CrealityDiscoveryDialog_hpp_
#define slic3r_CrealityDiscoveryDialog_hpp_

#include <wx/dialog.h>

#include <string>
#include <vector>

class wxWindow;

class wxListView;
class wxStaticText;

namespace Slic3r {
namespace GUI {

class CrealityDiscoveryDialog : public wxDialog
{
public:
    CrealityDiscoveryDialog(wxWindow* parent);
    ~CrealityDiscoveryDialog() override = default;

    std::string selected_ip() const { return m_selected_ip; }

private:
    void run_discovery();
    void on_ok();

    struct Row { std::string ip; std::string model; std::string hostname; };

    wxListView*   m_list = nullptr;
    wxStaticText* m_status = nullptr;
    std::vector<Row> m_rows;
    std::string   m_selected_ip;
};

}
}

#endif
