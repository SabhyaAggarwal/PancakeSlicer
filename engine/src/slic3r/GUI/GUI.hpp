#ifndef slic3r_GUI_hpp_
#define slic3r_GUI_hpp_

#include <string>
namespace boost { class any; }
namespace boost::filesystem { class path; }

#include <wx/string.h>

#include "libslic3r/Config.hpp"
#include "libslic3r/Preset.hpp"

class wxWindow;
class wxMenuBar;
class wxComboCtrl;
class wxFileDialog;
class wxTopLevelWindow;

namespace Slic3r { 

class AppConfig;
class DynamicPrintConfig;
class Print;

namespace GUI {

void disable_screensaver();
void enable_screensaver();
bool debugged();
void break_to_debugger();

extern const std::string& shortkey_ctrl_prefix();
extern const std::string& shortkey_alt_prefix();
extern const std::string& shortkey_shift_prefix();

extern AppConfig* get_app_config();

extern void add_menus(wxMenuBar *menu, int event_preferences_changed, int event_language_change);

void change_opt_value(DynamicPrintConfig& config, const t_config_option_key& opt_key, const boost::any& value, int opt_index = 0);

void show_error(wxWindow* parent, const wxString& message, bool has_code_excerpts = false);
void show_error(wxWindow* parent, const char* message, bool has_code_excerpts = false);
inline void show_error(wxWindow* parent, const std::string& message, bool has_code_excerpts = false) { show_error(parent, message.c_str(), has_code_excerpts); }
void show_error_id(int id, const std::string& message);
void show_info(wxWindow* parent, const wxString& message, const wxString& title = wxString());
void show_info(wxWindow* parent, const char* message, const char* title = nullptr);
inline void show_info(wxWindow* parent, const std::string& message,const std::string& title = std::string()) { show_info(parent, message.c_str(), title.c_str()); }
void warning_catcher(wxWindow* parent, const wxString& message);
void show_substitutions_info(const PresetsConfigSubstitutions& presets_config_substitutions);
void show_substitutions_info(const ConfigSubstitutions& config_substitutions, const std::string& filename);

void create_combochecklist(wxComboCtrl* comboCtrl, const std::string& text, const std::string& items);

unsigned int combochecklist_get_flags(wxComboCtrl* comboCtrl);

void combochecklist_set_flags(wxComboCtrl* comboCtrl, unsigned int flags);

wxString	from_u8(const std::string &str);
std::string	into_u8(const wxString &str);
wxString	from_path(const boost::filesystem::path &path);
boost::filesystem::path	into_path(const wxString &str);
wxString	file_url_from_path(const boost::filesystem::path &path);

extern void about();
extern void desktop_open_datadir_folder();
extern void desktop_open_any_folder(const std::string& path);
extern bool desktop_open_project_attachment(wxWindow *parent, const boost::filesystem::path &path);
}
}

#endif
