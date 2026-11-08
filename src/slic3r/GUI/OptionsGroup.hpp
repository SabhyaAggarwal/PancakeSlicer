#ifndef slic3r_OptionsGroup_hpp_
#define slic3r_OptionsGroup_hpp_

#include <exception>
#include <wx/sizer.h>
#include <memory>
#include <string>
#include <cstddef>
#include <vector>
#include <utility>
#include <boost/any.hpp>
#include <wx/stattext.h>
#include <wx/settings.h>

#include <map>
#include <functional>
#include <wx/string.h>

#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "Field.hpp"
#include "I18N.hpp"

#ifdef __WXOSX__
    #define wxOSX true
#else
    #define wxOSX false
#endif

#define BORDER(a, b) ((wxOSX ? a : b))

namespace Slic3r { namespace GUI {

class UIBuildCanceled : public std::exception {};
class OG_CustomCtrl;

using widget_t = std::function<wxSizer*(wxWindow*)>;
struct Option {
	ConfigOptionDef			opt { ConfigOptionDef() };
	t_config_option_key		opt_id;
    widget_t				side_widget {nullptr};
    bool					readonly {false};

	bool operator==(const Option& rhs) const {
		return  (rhs.opt_id == this->opt_id);
	}

	Option(const ConfigOptionDef& _opt, t_config_option_key id) :
		opt(_opt), opt_id(id) {}
};

class Line : public UndoValueUIManager
{
	bool		m_is_separator{ false };
public:
    wxString	label;
    wxString	label_tooltip;
	std::string	label_path;
    bool        undo_to_sys{false};
    bool        toggle_visible{true};

    size_t		full_width {0};
    widget_t	widget {nullptr};
    std::function<wxWindow*(wxWindow*)>	near_label_widget{ nullptr };
	wxWindow*	near_label_widget_win {nullptr};
    wxStaticText* label_widget {nullptr};
    wxSizer*	widget_sizer {nullptr};
    wxSizer*	extra_widget_sizer {nullptr};
    wxWindow*	extra_widget_win {nullptr};
    std::string& get_first_option_key() {
        return m_options[0].opt_id;
    }

    void append_option(const Option& option) {
        m_options.push_back(option);
    }
	void append_widget(const widget_t widget) {
		m_extra_widgets.push_back(widget);
    }
	Line(wxString label, wxString tooltip) :
		label(_(label)), label_tooltip(_(tooltip)) {}
	Line() : m_is_separator(true) {}

    void set_label(const wxString& new_label) {
        label = new_label;
        if (label_widget != nullptr) {
            label_widget->SetLabel(label + (label.IsEmpty() ? "" : ": "));
            label_widget->Refresh();
        }
    }

	bool is_separator() const { return m_is_separator; }
	bool has_only_option(const std::string& opt_key) const { return m_options.size() == 1 && m_options[0].opt_id == opt_key; }

    const std::vector<widget_t>&	get_extra_widgets() const {return m_extra_widgets;}
    const std::vector<Option>&		get_options() const { return m_options; }

private:
	std::vector<Option>		m_options;
    std::vector<widget_t>	m_extra_widgets;
};

using column_t = std::function<wxWindow*(wxWindow* parent, const Line&)>;

using t_optionfield_map = std::map<t_config_option_key, t_field>;
using t_opt_map = std::map< std::string, std::pair<std::string, int> >;

class OptionsGroup {
public:
    const bool staticbox{true};
    bool split_multi_line{false};
    bool option_label_at_right{false};
    wxWindow *     stb{ nullptr };
    const wxString  icon;
    const wxString  title;
    bool            m_labels_hidden{false};
    size_t			label_width = 20 ;
    wxSizer*		sizer {nullptr};
	OG_CustomCtrl*  custom_ctrl{ nullptr };
	int				ctrl_horiz_alignment{ wxALIGN_LEFT};
    column_t		extra_column {nullptr};
    t_change		m_on_change { nullptr };
    t_kill_focus    m_fill_empty_value { nullptr };
	std::function<DynamicPrintConfig()>	m_get_initial_config{ nullptr };
	std::function<DynamicPrintConfig()>	m_get_sys_config{ nullptr };
	std::function<bool()>	have_sys_config{ nullptr };

    std::function<void(wxWindow* win)> rescale_extra_column_item { nullptr };
    std::function<void(wxWindow* win)> rescale_near_label_widget { nullptr };

    std::function<void(const t_config_option_key& opt_key)> edit_custom_gcode { nullptr };

    wxFont			sidetext_font {wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT) };
    wxFont			label_font {wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT) };
	int				sidetext_width{ -1 };
	int				sublabel_width{ -1 };
	bool			draw_multi_extruder { false };

    inline wxWindow* parent() const { return m_parent; }

    wxWindow*   ctrl_parent() const;

	void		append_line(const Line& line);
	void		activate_line(Line& line);
	Line* get_line(const std::string& opt_key);

	bool		activate(std::function<void()> throw_if_canceled = [](){}, int horiz_alignment = wxALIGN_LEFT);
	void		clear(bool destroy_custom_ctrl = false);

    Line		create_single_option_line(const Option& option, const std::string& path = std::string()) const;
    void		append_single_option_line(const Option& option, const std::string& path = std::string()) { append_line(create_single_option_line(option, path)); }
	void		append_separator();

    inline Field*	get_field(const t_config_option_key& id) const{
							if (m_fields.find(id) == m_fields.end()) return nullptr;
							return m_fields.at(id).get();
    }

	bool			set_value(const t_config_option_key& id, const boost::any& value, bool change_event = false) {
							if (m_fields.find(id) == m_fields.end()) return false;
							m_fields.at(id)->set_value(value, change_event);
							return true;
    }
	boost::any		get_value(const t_config_option_key& id) {
							boost::any out;
    						if (m_fields.find(id) == m_fields.end()) ;
							else
								out = m_fields.at(id)->get_value();
							return out;
    }

	void			show_field(const t_config_option_key& opt_key, bool show = true);
	void			hide_field(const t_config_option_key& opt_key) {  show_field(opt_key, false);  }

	void enable_field(const t_config_option_key& opt_key, bool enable = true);
    void disable_field(const t_config_option_key& opt_key) { enable_field(opt_key, false); }

	void			set_name(const wxString& new_name);

	inline void		enable() { for (auto& field : m_fields) field.second->enable(); }
    inline void		disable() { for (auto& field : m_fields) field.second->disable(); }
	void			set_grid_vgap(int gap) { m_grid_sizer->SetVGap(gap); }

    void            clear_fields_except_of(const std::vector<std::string> left_fields);

    void            hide_labels() { label_width = 0; m_labels_hidden = true; }

	OptionsGroup(wxWindow *_parent, const wxString &title, const wxString &icon, bool is_tab_opt = false,
                    column_t extra_clmn = nullptr);
	virtual ~OptionsGroup() { clear(true); }

    wxGridSizer*        get_grid_sizer() { return m_grid_sizer; }
	const std::vector<Line>& get_lines() { return m_lines; }
	bool				is_legend_line();
	void				set_max_win_width(int max_win_width);

	bool				is_activated() { return sizer != nullptr; }

	void remove_option_if(std::function<bool(std::string const &)> const & comp);
protected:
	std::map<t_config_option_key, Option>	m_options;
    wxWindow*				m_parent {nullptr};
    std::vector<ConfigOptionMode>           m_options_mode;
    std::vector<wxWindow*>                  m_extra_column_item_ptrs;

    std::vector<Line>                       m_lines;

    t_optionfield_map		m_fields;
    bool					m_disabled {false};
    wxGridSizer*			m_grid_sizer {nullptr};
	bool					m_use_custom_ctrl{ false };

	bool					m_use_custom_ctrl_as_parent { false };

#if 0
	wxPanel*				m_panel {nullptr};
#endif
	const t_field&		build_field(const t_config_option_key& id, const ConfigOptionDef& opt);
	const t_field&		build_field(const t_config_option_key& id);
	const t_field&		build_field(const Option& opt);

    virtual void		on_kill_focus(const std::string& opt_key) {};
	virtual void		on_change_OG(const t_config_option_key& opt_id, const boost::any& value);
	virtual void		back_to_initial_value(const std::string& opt_key) {}
	virtual void		back_to_sys_value(const std::string& opt_key) {}

	virtual int			config_type() const { return -1; }

public:
	static wxString		get_url(const std::string& path_end);
	static bool			launch_browser(const std::string& path_end);

protected:
    std::string         pick_plugin(const ConfigOptionDef& opt);
};

class ConfigOptionsGroup: public OptionsGroup {
public:
	ConfigOptionsGroup(	wxWindow* parent, const wxString& title, const wxString& icon, DynamicPrintConfig* config = nullptr,
						bool is_tab_opt = false, column_t extra_clmn = nullptr) :
		OptionsGroup(parent, title, icon, is_tab_opt, extra_clmn), m_config(config) {}
	ConfigOptionsGroup(	wxWindow* parent, const wxString& title, DynamicPrintConfig* config = nullptr,
						bool is_tab_opt = false, column_t extra_clmn = nullptr) :
		ConfigOptionsGroup(parent, title, wxEmptyString, config, is_tab_opt, extra_clmn) {}
	ConfigOptionsGroup(	wxWindow* parent, const wxString& title, ModelConfig* config,
						bool is_tab_opt = false, column_t extra_clmn = nullptr) :
		OptionsGroup(parent, title, wxEmptyString, is_tab_opt, extra_clmn), m_config(&config->get()), m_modelconfig(config) {}
	ConfigOptionsGroup(	wxWindow* parent) :
		OptionsGroup(parent, wxEmptyString, wxEmptyString, true, nullptr) {}

	const wxString& config_category() const throw() { return m_config_category; }
	int config_type() const throw() override { return m_config_type; }
	const t_opt_map&   opt_map() const throw() { return m_opt_map; }

	void 		set_config_category_and_type(const wxString &category, int type) { m_config_category = category; m_config_type = type; }
    void        set_config(DynamicPrintConfig* config) {
		m_config = config; m_modelconfig = nullptr; }
	Option		get_option(const std::string& opt_key, int opt_index = -1);
	Line		create_single_option_line(const std::string& title, const std::string& path = std::string(), int idx = -1) {
		Option option = get_option(title, idx);
		return OptionsGroup::create_single_option_line(option, path);
	}
	Line		create_single_option_line(const Option& option, const std::string& path = std::string()) const {
		return OptionsGroup::create_single_option_line(option, path);
	}
	void		append_single_option_line(const Option& option, const std::string& path = std::string())	{
		OptionsGroup::append_single_option_line(option, path);
	}
	void		append_single_option_line(const std::string title, const std::string& path = std::string(), int idx = -1)
	{
		Option option = get_option(title, idx);
		append_single_option_line(option, path);
	}

	void		on_change_OG(const t_config_option_key& opt_id, const boost::any& value) override;
	void		back_to_initial_value(const std::string& opt_key) override;
	void		back_to_sys_value(const std::string& opt_key) override;
	void		back_to_config_value(const DynamicPrintConfig& config, const std::string& opt_key);
    void		on_kill_focus(const std::string& opt_key) override;
	void		reload_config();
    void        Hide();
    void        Show(const bool show);
    bool        is_visible(ConfigOptionMode mode);
    bool        update_visibility(ConfigOptionMode mode);
    void        msw_rescale();
    void        sys_color_changed();
    void        refresh();
	boost::any	config_value(const std::string& opt_key, int opt_index, bool deserialize);
	boost::any	get_config_value(const DynamicPrintConfig& config, const std::string& opt_key, int opt_index = -1);
	boost::any	get_config_value2(const DynamicPrintConfig& config, const std::string& opt_key, int opt_index = -1);
	Field*		get_fieldc(const t_config_option_key& opt_key, int opt_index);
	std::pair<OG_CustomCtrl*, bool*>	get_custom_ctrl_with_blinking_ptr(const t_config_option_key& opt_key, int opt_index );

protected:
    const DynamicPrintConfig*	m_config {nullptr};
    ModelConfig*				m_modelconfig { nullptr };
	t_opt_map					m_opt_map;
    wxString                    m_config_category;
    int                         m_config_type;

	void 	change_opt_value(const t_config_option_key& opt_key, const boost::any& value, int opt_index = 0);
};

class ExtruderOptionsGroup : public ConfigOptionsGroup {
public:
	ExtruderOptionsGroup(wxWindow* parent, const wxString& title, const wxString& icon, DynamicPrintConfig* config = nullptr,
		bool is_tab_opt = false, column_t extra_clmn = nullptr) :
		ConfigOptionsGroup(parent, title, icon, config, is_tab_opt, extra_clmn) {}

	void on_change_OG(const t_config_option_key& opt_id, const boost::any& value) override;
};

class ogStaticText :public wxStaticText{
public:
	ogStaticText() {}
	ogStaticText(wxWindow* parent, const wxString& text);
	~ogStaticText() {}

	void		SetText(const wxString& value, bool wrap = true);
	void		SetPathEnd(const std::string& link);
	void		FocusText(bool focus);
};

}}

#endif
