#include <string>
#ifdef __linux__
#ifndef slic3r_DesktopIntegrationDialog_hpp_
#define slic3r_DesktopIntegrationDialog_hpp_

#include <wx/dialog.h>

namespace Slic3r {
namespace GUI {
class DesktopIntegrationDialog : public wxDialog
{
public:
	DesktopIntegrationDialog(wxWindow *parent);
	DesktopIntegrationDialog(DesktopIntegrationDialog &&) = delete;
	DesktopIntegrationDialog(const DesktopIntegrationDialog &) = delete;
	DesktopIntegrationDialog &operator=(DesktopIntegrationDialog &&) = delete;
	DesktopIntegrationDialog &operator=(const DesktopIntegrationDialog &) = delete;
	~DesktopIntegrationDialog();

	static bool is_integrated();
	static bool integration_possible();
	static void perform_desktop_integration();
	static void undo_desktop_intgration();
	
	static void perform_downloader_desktop_integration(std::string url_prefix);
	static void undo_downloader_registration();
private:

};
}
}

#endif
#endif
