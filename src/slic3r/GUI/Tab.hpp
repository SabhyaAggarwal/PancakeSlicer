#ifndef slic3r_Tab_hpp_
#define slic3r_Tab_hpp_

#include "libslic3r/PrintConfig.hpp"
#include <cstddef>
#include "libslic3r/Config.hpp"
#include <functional>
#include "slic3r/GUI/Field.hpp"
#include <boost/any.hpp>
#include <string>
#include <wx/event.h>
#include <utility>
#include <wx/anybutton.h>
#include <wx/panel.h>
#include <wx/notebook.h>
#include <wx/listbook.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/string.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>

#include <map>
#include <vector>
#include <memory>

#include "wxExtensions.hpp"
#include "ConfigManipulation.hpp"
#include "OptionsGroup.hpp"
#include "libslic3r/Preset.hpp"
#include "Notebook.hpp"
#include "ParamsPanel.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"

class TabCtrl;
class ModeSwitchButton;
class SwitchButton;
class MultiSwitchButton;

namespace Slic3r {

class ModelConfig;
class ObjectBase;

namespace GUI {

class TabPresetComboBox;
class OG_CustomCtrl;

std::vector<InputShaperType> input_shaper_types_for_flavor(GCodeFlavor flavor);

using ConfigOptionsGroupShp = std::shared_ptr<ConfigOptionsGroup>;
class Page: public std::enable_shared_from_this<Page>
{
	wxPanel*		m_tab_owner;
	wxWindow*		m_parent;
	wxString		m_title;
	size_t			m_iconID;
	wxBoxSizer*		m_vsizer;
	wxStaticText*	m_page_title;
    bool            m_show = true;
    bool            m_visibility_applied = false;
public:
    Page(wxWindow* parent, const wxString& title, int iconID, wxPanel* tab_owner);
	~Page() {}

	bool				m_is_modified_values{ false };
	bool				m_is_nonsys_values{ true };
    bool            m_split_multi_line      = false;
    bool            m_option_label_at_right = false;

public:
	std::vector <ConfigOptionsGroupShp> m_optgroups;
	DynamicPrintConfig* m_config;

	wxBoxSizer*	vsizer() const { return m_vsizer; }
	wxWindow*	parent() const { return m_parent; }
	const wxString&	title()	 const { return m_title; }
	size_t		iconID() const { return m_iconID; }
	void		set_config(DynamicPrintConfig* config_in) { m_config = config_in; }
	void		reload_config();
    void        update_visibility(ConfigOptionMode mode, bool update_contolls_visibility);
    void        activate(ConfigOptionMode mode, std::function<void()> throw_if_canceled);
    bool        build_pending() const;
    bool        build_step(ConfigOptionMode mode);
    bool        visibility_pending() const { return !m_visibility_applied; }
    void        clear();
    void        msw_rescale();
    void        sys_color_changed();
    void        refresh();
	Field*		get_field(const t_config_option_key& opt_key, int opt_index = -1) const;
    Line *      get_line(const t_config_option_key &opt_key, int opt_index = -1);
	bool		set_value(const t_config_option_key& opt_key, const boost::any& value);
	ConfigOptionsGroupShp	new_optgroup(const wxString& title, const wxString& icon = wxEmptyString, int noncommon_label_width = -1, bool is_extruder_og = false);
	const ConfigOptionsGroupShp	get_optgroup(const wxString& title) const;

	bool		set_item_colour(const wxColour *clr) {
		if (m_item_color != clr) {
			m_item_color = clr;
			return true;
		}
		return false;
	}

	const wxColour	get_item_colour() {
			return *m_item_color;
	}
    bool get_show() const { return m_show; }

    std::map<std::string, std::string> m_opt_id_map;

protected:
    size_t      next_group_to_build() const;
    bool        activate_group(size_t i, ConfigOptionMode mode, std::function<void()> throw_if_canceled);
	const wxColour*		m_item_color;
};


using PageShp = std::shared_ptr<Page>;
class Tab: public wxPanel
{
protected:
	ParamsPanel*		m_parent;

    Preset::Type        m_type;
	std::string			m_name;
	const wxString		m_title;
	TabPresetComboBox*	m_presets_choice { nullptr };

	wxPanel*			m_top_panel;
	wxBoxSizer* m_main_sizer;
	wxBoxSizer* m_top_sizer;
	wxBoxSizer* m_top_left_sizer;
	wxGridSizer* m_top_right_sizer;
	wxBoxSizer* m_select_sizer;
	wxBoxSizer* m_tree_sizer;

	ScalableButton*		m_btn_compare_preset;
	ScalableButton*		m_btn_save_preset;
	ScalableButton*		m_btn_delete_preset;
	TabCtrl*			m_tabctrl;
	wxImageList*		m_icons;

	wxScrolledWindow*	m_page_view {nullptr};

   	struct PresetDependencies {
		Preset::Type type	  = Preset::TYPE_INVALID;
		::CheckBox*   checkbox = nullptr;
		wxStaticText* checkbox_title = nullptr;
		Button 	*btn  = nullptr;
		std::string  key_list;
		std::string  key_condition;
		wxString     dialog_title;
		wxString     dialog_label;
	};
	PresetDependencies 	m_compatible_printers;
	PresetDependencies 	m_compatible_prints;

    bool                    m_is_default_preset {false};

    bool m_just_edit{false};

	ScalableButton*			m_undo_btn;
	ScalableButton*			m_undo_to_sys_btn;
	ScalableButton*			m_btn_search;
    StaticBox *				m_search_item;
    TextInput *				m_search_input;

	ScalableBitmap			m_bmp_show_incompatible_presets;
	ScalableBitmap			m_bmp_hide_incompatible_presets;
	ScalableBitmap 			m_bmp_value_lock;
	ScalableBitmap 			m_bmp_value_unlock;
	ScalableBitmap 			m_bmp_white_bullet;
	ScalableBitmap 		   *m_bmp_non_system;
	ScalableBitmap 			m_bmp_value_revert;
    ScalableBitmap 			m_bmp_edit_value;

    std::vector<ScalableButton*>	m_scaled_buttons = {};
    std::vector<ScalableBitmap*>	m_scaled_bitmaps = {};
    std::vector<ScalableBitmap>     m_scaled_icons_list = {};

	wxColour			m_sys_label_clr;
	wxColour			m_modified_label_clr;
	wxColour			m_default_text_clr;

	wxString			m_ttg_value_lock;
	wxString			m_ttg_value_unlock;
	wxString			m_ttg_white_bullet_ns;
	wxString			*m_ttg_non_system;
	wxString			m_ttg_white_bullet;
	wxString			m_ttg_value_revert;

	wxString			m_tt_value_lock;
	wxString			m_tt_value_unlock;
	wxString			*m_tt_non_system;
	wxString			m_tt_white_bullet;
	wxString			m_tt_value_revert;

	int					m_icon_count;
	std::map<std::string, size_t>	m_icon_index;
	std::map<wxString, std::string>	m_category_icon;
	std::vector<PageShp>			m_pages;
	Page*				m_active_page {nullptr};
	bool				m_disable_tree_sel_changed_event {false};
	bool				m_show_incompatible_presets;
	int					m_last_select_item = -1;

    std::vector<Preset::Type>	m_dependent_tabs;
	enum OptStatus { osSystemValue = 1, osInitValue = 2 };
	std::map<std::string, int>	m_options_list;
    std::map<std::string, int> m_all_extruder_options_status;
	int							m_opt_status_value = 0;

	bool				m_is_modified_values{ false };
	bool				m_is_nonsys_values{ true };
	bool				m_postpone_update_ui {false};

    void                set_type();

    int                 m_em_unit;
    bool                m_completed { false };
    ConfigOptionMode    m_mode = comAdvanced;

	struct Highlighter
	{
		void set_timer_owner(wxEvtHandler* owner, int timerid = wxID_ANY);
		void init(std::pair<OG_CustomCtrl*, bool*>);
		void blink();
		void invalidate();

	private:
		OG_CustomCtrl*	m_custom_ctrl	{nullptr};
		bool*			m_show_blink_ptr{nullptr};
		int				m_blink_counter	{0};
	    wxTimer         m_timer;
	}
    m_highlighter;

	DynamicPrintConfig 	m_cache_config;
    std::vector<std::string> m_cache_options;


	bool				m_page_switch_running = false;
	bool				m_page_switch_planned = false;

    bool				m_is_timelapse_wipe_tower_already_prompted = false;

public:
	PresetBundle*		m_preset_bundle;
	bool				m_show_btn_incompatible_presets = false;
	PresetCollection*	m_presets = nullptr;
	DynamicPrintConfig*	m_config;
	ogStaticText*		m_parent_preset_description_line = nullptr;
	ScalableButton*		m_detach_preset_btn	= nullptr;

	std::map<std::string, wxColour>	m_colored_Label_colors;

    int                 m_update_cnt = 0;

	ModeSwitchButton *m_mode_view = nullptr;
	ScalableButton* m_mode_icon = nullptr;
    wxSizer *       m_variant_sizer   = nullptr;
    MultiSwitchButton *  m_extruder_switch = nullptr;
    MultiSwitchButton *  m_variant_combo   = nullptr;
    ScalableButton *m_extruder_sync   = nullptr;
	wxPanel *       m_extruder_sync_box  = nullptr;
    bool            m_extruder_switch_variants = false;
    std::vector<NozzleVolumeType> m_actual_nozzle_volumes;

public:
	Tab(ParamsPanel* parent, const wxString& title, Preset::Type type);

    ~Tab() {}

	wxWindow*	parent() const { return m_parent; }
	wxString	title()	 const { return m_title; }
	std::string	name()	 const { return m_presets->name(); }
    Preset::Type type()  const { return m_type; }
    bool 		completed() const { return m_completed; }
	virtual bool supports_printer_technology(const PrinterTechnology tech) const = 0;

	void		create_preset_tab();
    void        add_scaled_button(wxWindow* parent, ScalableButton** btn, const std::string& icon_name,
                                  const wxString& label = wxEmptyString,
                                  long style = wxBU_EXACTFIT | wxNO_BORDER);
    void        add_scaled_bitmap(wxWindow* parent, ScalableBitmap& btn, const std::string& icon_name);
	void		update_ui_items_related_on_parent_preset(const Preset* selected_preset_parent);
    void		load_current_preset();
    void        reactive_preset_combo_box();
	void        rebuild_page_tree();
    void		update_btns_enabling();
    void		update_preset_choice();
    bool select_preset(std::string preset_name = "", bool delete_current = false, const std::string &last_selected_ph_printer_name = "", bool force_select = false, bool force_no_transfer = false);
	bool		may_discard_current_dirty_preset(PresetCollection* presets = nullptr, const std::string& new_printer_name = "", bool no_transfer = false, bool no_transfer_variant = false);

    virtual void    clear_pages();
    virtual void    update_description_lines();
    virtual void    activate_selected_page(std::function<void()> throw_if_canceled);

	void		OnTreeSelChange(wxCommandEvent& event);
	void		OnKeyDown(wxKeyEvent& event);

	void		compare_preset();
	void		transfer_options(const std::string&name_from, const std::string&name_to, std::vector<std::string> options);
	void        save_preset(std::string name = std::string(), bool detach = false, bool save_to_project = false, bool from_input = false, std::string input_name = "");

	void		delete_preset();
	void		toggle_show_hide_incompatible();
	void		update_show_hide_incompatible_button();
	void		update_ui_from_settings();
	void		update_label_colours();
	void		decorate();
	void		update_changed_ui();
	void		get_sys_and_mod_flags(const std::string& opt_key, bool& sys_page, bool& modified_page);
    void        update_changed_tree_ui();
	void		update_undo_buttons();
    void        update_extruder_switch_colors();
    bool        variant_switch_active() const;
    bool        is_printer_extruder_page(const Page* page) const { return m_type == Preset::TYPE_PRINTER && page && page->title() == "Extruder"; }
    void        update_variant_sizer_visibility();
    void        update_all_extruder_options_status();
    void        check_extruder_options_status(int index, bool &sys_extruder, bool &modified_extruder, const std::vector<PageShp>& pages_to_check);

	void		on_roll_back_value(const bool to_sys = false);

	PageShp		add_options_page(const wxString& title, const std::string& icon, bool is_extruder_pages = false);
	static wxString translate_category(const wxString& title, Preset::Type preset_type);

	virtual void	OnActivate();
	virtual void	on_preset_loaded() {}
	virtual void	build() = 0;
	virtual void	update() = 0;
	virtual void	toggle_options() = 0;
	virtual void	init_options_list();
	std::string	options_list_storage_key(const std::string& opt_key) const;
    virtual void    update_custom_dirty(std::vector<std::string> &dirty_options, std::vector<std::string> &nonsys_options) {}
	void			load_initial_data();
	void			update_dirty();
	void			update_tab_ui(bool update_plater_presets = false);
	void			load_config(const DynamicPrintConfig& config);
	virtual void	reload_config();
    void            update_mode();
    void            update_visibility();
    virtual void    msw_rescale();
    virtual void	sys_color_changed();
	Field*			get_field(const t_config_option_key& opt_key, int opt_index = -1) const;
	Line*			get_line(const t_config_option_key& opt_key);
	std::pair<OG_CustomCtrl*, bool*> get_custom_ctrl_with_blinking_ptr(const t_config_option_key& opt_key, int opt_index = -1);

    Field*          get_field(const t_config_option_key &opt_key, Page** selected_page, int opt_index = -1);
    void            toggle_option(const std::string &opt_key, bool toggle, int opt_index = -1);
    void            toggle_line(const std::string &opt_key, bool toggle, int opt_index = -1);
    void            set_option_label(const std::string &opt_key, const wxString &label, int opt_index = -1);

    struct SettingRowState
    {
        bool     visible{true};
        wxString label;
        bool     multi{false};
    };
    SettingRowState setting_row_state(const std::string &opt_id) const;

	wxSizer*		description_line_widget(wxWindow* parent, ogStaticText** StaticText, wxString text = wxEmptyString);
	bool			current_preset_is_dirty() const;
	bool			saved_preset_is_dirty() const;
	void            update_saved_preset_from_current_preset();
    void            update_pages_with_multi_variant();

	DynamicPrintConfig*	get_config() { return m_config; }
    PresetCollection *  get_presets() { return m_presets; }
    TabPresetComboBox *  get_combo_box() { return m_presets_choice; }

	virtual void    on_value_change(const std::string& opt_key, const boost::any& value);

    void            update_wiping_button_visibility();
	virtual void	activate_option(const std::string& opt_key, const wxString& category);
    void			apply_searcher();
	void			cache_config_diff(const std::vector<std::string>& selected_options, const DynamicPrintConfig* config = nullptr);
	void			apply_config_from_cache();
    void            show_timelapse_warning_dialog();

	const std::map<wxString, std::string>& get_category_icon_map() { return m_category_icon; }
	bool update_current_page_in_background(int& item);
	void unselect_tree_item();
	void set_expanded(bool value);
	void restore_last_select_item();
	bool page_build_pending() const;
	bool page_build_step();

	static bool validate_custom_gcode(const wxString& title, const std::string& gcode);
	bool        validate_custom_gcodes();
	bool        validate_filament_temperature_pairs();
    bool        validate_custom_gcodes_was_shown{ false };
    void        set_just_edit(bool just_edit);

    void						edit_custom_gcode(const t_config_option_key& opt_key);
    virtual const std::string&	get_custom_gcode(const t_config_option_key& opt_key);
    virtual void				set_custom_gcode(const t_config_option_key& opt_key, const std::string& value);

    void        update_extruder_variants(int extruder_id = -1, bool reload = true);
    void        switch_excluder(int extruder_id = -1, bool reload = true);
    void        sync_excluder();
	void        parse_extruder_selection(int selection, int &extruder_id, NozzleVolumeType &nozzle_type);
    int         calculate_selection_index_for_extruder(int extruder_id, NozzleVolumeType nozzle_type);
	bool        get_extruder_sync_enable_state(int extruder_id);
	int         get_current_active_extruder();

	std::vector<wxString>  generate_extruder_options();
    NozzleVolumeType       get_actual_nozzle_volume_type(int extruder_id);

protected:
	void			create_line_with_widget(ConfigOptionsGroup* optgroup, const std::string& opt_key, const std::string& path, widget_t widget);
	wxSizer*		compatible_widget_create(wxWindow* parent, PresetDependencies &deps);
	void 			compatible_widget_reload(PresetDependencies &deps);
	void			load_key_value(const std::string& opt_key, const boost::any& value, bool saved_value = false);

	bool			tree_sel_change_delayed(wxCommandEvent& event);
	void			on_presets_changed();
	void			update_printer_agent_if_needed();
	void			build_preset_description_line(ConfigOptionsGroup* optgroup);
	void			update_preset_description_line();
	void			update_frequently_changed_parameters();
	void			set_tooltips_text();
    void			filter_diff_option(std::vector<std::string> &options);

    ConfigManipulation m_config_manipulation;
    std::string m_last_sparse_infill_rotate_template_value;
    ConfigManipulation get_config_manipulation();
    friend class EditGCodeDialog;
    friend class PublishSettingsDialog;
};

class TabPrint : public Tab
{
public:
	TabPrint(ParamsPanel* parent, Preset::Type type = Preset::TYPE_PRINT) :
        Tab(parent, _(L("Process")), type) {}
	~TabPrint() {}

	void		build() override;
	void		reload_config() override;
	void		update_description_lines() override;
	void		toggle_options() override;
	void		update() override;
	void		clear_pages() override;
	bool 		supports_printer_technology(const PrinterTechnology tech) const override { return tech == ptFFF; }

private:
	ogStaticText*	m_recommended_thin_wall_thickness_description_line = nullptr;
	ogStaticText*	m_top_bottom_shell_thickness_explanation = nullptr;
};

class TabPrintModel : public TabPrint
{
public:
	TabPrintModel(ParamsPanel* parent, std::vector<std::string> const & keys);
	~TabPrintModel() {}

	void build() override;

	void set_model_config(std::map<ObjectBase *, ModelConfig *> const & object_configs);

	bool has_model_config() const { return !m_object_configs.empty(); }

	void update_model_config();

	virtual void reset_model_config();

	bool has_key(std::string const &key);

protected:
	virtual void    activate_selected_page(std::function<void()> throw_if_canceled) override;

	virtual void    on_value_change(const std::string& opt_key, const boost::any& value) override;

	virtual void    notify_changed(ObjectBase * object) = 0;

	virtual void	reload_config() override;

	virtual void	update_custom_dirty(std::vector<std::string> &dirty_options, std::vector<std::string> &nonsys_options) override;

protected:
	std::vector<std::string> m_keys;
	PresetCollection m_prints;
	Tab * m_parent_tab;
	std::map<ObjectBase *, ModelConfig *> m_object_configs;
	std::vector<std::string> m_all_keys;
	std::vector<std::string> m_null_keys;
	bool m_back_to_sys = false;
};


class TabPrintPlate : public TabPrintModel
{
public:
	TabPrintPlate(ParamsPanel* parent);
	~TabPrintPlate() {}
	void build() override;
	void reset_model_config() override;
	int show_spiral_mode_settings_dialog(bool is_object_config) { return m_config_manipulation.show_spiral_mode_settings_dialog(is_object_config); }
	void update_mixed_filament_seq_state();

protected:
	virtual void    on_value_change(const std::string& opt_key, const boost::any& value) override;
	virtual void    notify_changed(ObjectBase* object) override;
	virtual void	update_custom_dirty(std::vector<std::string> &dirty_options, std::vector<std::string> &nonsys_options) override;
};

class TabPrintObject : public TabPrintModel
{
public:
	TabPrintObject(ParamsPanel* parent);
	~TabPrintObject() {}
protected:
	virtual void    notify_changed(ObjectBase * object) override;
};

class TabPrintPart : public TabPrintModel
{
public:
	TabPrintPart(ParamsPanel* parent);
	~TabPrintPart() {}
protected:
	virtual void    notify_changed(ObjectBase * object) override;
};

class TabPrintLayer : public TabPrintModel
{
public:
	TabPrintLayer(ParamsPanel* parent);
	~TabPrintLayer() {}
protected:
	virtual void    notify_changed(ObjectBase* object) override;
	virtual void    update_custom_dirty(std::vector<std::string> &dirty_options, std::vector<std::string> &nonsys_options) override;
};

class TabFilament : public Tab
{
private:
	ogStaticText*	m_volumetric_speed_description_line {nullptr};
	ogStaticText*	m_cooling_description_line {nullptr};

    void            add_filament_overrides_page();
    void            update_filament_overrides_page(const DynamicPrintConfig* printers_config);
	void 			update_volumetric_flow_preset_hints();
    unsigned int    selected_variant_index() const;

    std::map<std::string, ::CheckBox*> m_overrides_options;

public:
	TabFilament(ParamsPanel* parent) :
		Tab(parent, _(L("Filament")), Slic3r::Preset::TYPE_FILAMENT) {}
	~TabFilament() {}

	void		build() override;
	void		reload_config() override;
	void		update_description_lines() override;
	void		toggle_options() override;
	void		update() override;
    void        init_options_list() override;
    void        clear_pages() override;
	bool 		supports_printer_technology(const PrinterTechnology tech) const override { return tech == ptFFF; }

	void		on_value_change(const std::string& opt_key, const boost::any& value) override;

    const std::string&	get_custom_gcode(const t_config_option_key& opt_key) override;
    void				set_custom_gcode(const t_config_option_key& opt_key, const std::string& value) override;
};

class IMEXModesCtrl;

class TabPrinter : public Tab
{
private:
	bool		m_use_silent_mode = false;
	void		append_option_line(ConfigOptionsGroupShp optgroup, const std::string opt_key, const std::string& label_path = "");
	bool		m_rebuild_kinematics_page = false;
	void        update_input_shaper_menu(GCodeFlavor flavor);

	bool		m_was_belt_printer = false;
	double		m_belt_synced_tilt_x = 0.;
	double		m_belt_synced_tilt_y = 0.;

    std::vector<PageShp>			m_pages_fff;
    std::vector<PageShp>			m_pages_sla;

    IMEXModesCtrl*      m_imex_modes_ctrl               {nullptr};
public:
	void		select_nonplanar_head(const std::string &id);
	ScalableButton*	m_reset_to_filament_color = nullptr;

	size_t		m_extruders_count;
	size_t		m_extruders_count_old = 0;
	size_t		m_initial_extruders_count;
	size_t		m_sys_extruders_count;
	size_t		m_cache_extruder_count = 0;
	std::vector<std::string> m_extruder_variant_list;
	std::string m_base_preset_name;

    PrinterTechnology               m_printer_technology = ptFFF;

    TabPrinter(ParamsPanel* parent) :
        Tab(parent, _L("Machine"), Slic3r::Preset::TYPE_PRINTER) {}
	~TabPrinter() {}

	void		build() override;
    void		build_fff();
    void		build_sla();
	void		reload_config() override;
	void		activate_selected_page(std::function<void()> throw_if_canceled) override;
	void		clear_pages() override;
	void		toggle_options() override;
    void		update() override;
    void		update_fff();
    void		update_sla();
    void        update_pages();
	void        on_gcode_flavor_changed();
	void		extruders_count_changed(size_t extruders_count);
	PageShp		build_kinematics_page();
	void		build_unregular_pages(bool from_initial_build = false);
	void		on_preset_loaded() override;
	void		init_options_list() override;
	void		msw_rescale() override;
	bool 		supports_printer_technology(const PrinterTechnology ) const override { return true; }

	void		set_extruder_volume_type(int extruder_id, NozzleVolumeType type);
	void		on_value_change(const std::string& opt_key, const boost::any& value) override;

	wxSizer*	create_bed_shape_widget(wxWindow* parent);
	void		cache_extruder_cnt(const DynamicPrintConfig* config = nullptr);
	bool		apply_extruder_cnt_from_cache();
	void		refresh_printer_agent_dropdown() const;

	Page*		extruder_page() const;
	int			extruder_page_data_index(const std::string& field_id) const;
	int			extruder_variant_index(int extruder);
	void		sync_extruders_count();
	void		activate_option(const std::string& opt_key, const wxString& category) override;

protected:
	void		update_custom_dirty(std::vector<std::string> &dirty_options, std::vector<std::string> &nonsys_options) override;
};

class TabSLAMaterial : public Tab
{
public:
    TabSLAMaterial(ParamsPanel* parent) :
		Tab(parent, _(_devL("Material Settings")), Slic3r::Preset::TYPE_SLA_MATERIAL) {}
    ~TabSLAMaterial() {}

	void		build() override;
	void		reload_config() override;
	void		toggle_options() override;
	void		update() override;
	bool 		supports_printer_technology(const PrinterTechnology tech) const override { return tech == ptSLA; }
};

class TabSLAPrint : public Tab
{
public:
    TabSLAPrint(ParamsPanel* parent) :
        Tab(parent, _(L("Process Settings")), Slic3r::Preset::TYPE_SLA_PRINT) {}
    ~TabSLAPrint() {}

	ogStaticText* m_support_object_elevation_description_line = nullptr;

    void		build() override;
	void		reload_config() override;
	void		update_description_lines() override;
	void		toggle_options() override;
    void		update() override;
	void		clear_pages() override;
	bool 		supports_printer_technology(const PrinterTechnology tech) const override { return tech == ptSLA; }
};

}
}

#endif
