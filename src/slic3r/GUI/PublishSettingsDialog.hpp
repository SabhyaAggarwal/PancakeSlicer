#pragma once

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"

#include "libslic3r/PublishSettings.hpp"

#include <cstddef>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/panel.h>
#include <wx/gdicmn.h>
#include <wx/event.h>
#include <wx/wx.h>
#include <wx/colour.h>
#include <wx/scrolwin.h>
#include <wx/menu.h>
#include <utility>
#include <vector>
#include <string>

class TabCtrl;
class wxBoxSizer;
class wxCommandEvent;
class wxMouseEvent;
class wxPanel;
class wxScrolledWindow;
class wxSizerItem;
class wxStaticBitmap;
class wxStaticText;
class wxTextCtrl;
class wxWindow;
namespace Slic3r { class DynamicPrintConfig; }

class TextInput;
class StaticLine;
class CheckBox;
class MultiSwitchButton;

namespace Slic3r { namespace GUI {

struct PublishMaterialIdentity
{
    std::string type;
    std::string vendor;
    std::string id;
};

struct MixedDependencyIssue
{
    enum class Reason { Disabled, MaterialNotPublished };
    size_t mixed_slot{0};
    size_t component_slot{0};
    Reason reason{Reason::Disabled};
};

class PublishSettingsDialog : public DPIDialog
{
public:
    PublishSettingsDialog(wxWindow* parent = nullptr,
                          const std::vector<std::string>* published_keys = nullptr,
                          const std::vector<Slic3r::PublishedMaterialEntry>* material_keys = nullptr);
    ~PublishSettingsDialog();

    std::vector<std::string> GetPublishedKeys() const;

    std::vector<Slic3r::PublishedMaterialEntry> GetPublishedMaterialKeys() const;

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override;

private:
    void fit_to_content();
    void refresh_mixed_tab_bitmaps();

    enum class Section { Print, Printer, Material };

    enum class RowKind {
        Setting,
        Color,
        Type,
    };
    struct Row
    {
        std::string key;
        wxString category;
        wxString subcategory;
        wxString label;
        wxString value;
        wxString unit;
        wxString section_title;
        Section section{Section::Print};
        RowKind kind{RowKind::Setting};
        size_t outer_index{0};
        size_t inner_index{0};
        bool dirty{false};
        bool matches_filter{false};
        ::CheckBox* check{nullptr};
        wxStaticText* check_label{nullptr};
        wxStaticText* value_label{nullptr};
        wxStaticText* unit_label{nullptr};
        wxStaticBitmap* color_chip{nullptr};
        wxSizerItem* item{nullptr};
    };

    struct Subcategory
    {
        wxString title;
        ::StaticLine* header{nullptr};
        wxSizerItem* item{nullptr};
        std::vector<size_t> rows;
    };

    struct Category
    {
        wxString title;
        Section section{Section::Print};
        size_t group{0};
        size_t source_index{0};
        wxPanel* page{nullptr};
        wxScrolledWindow* scroll{nullptr};
        wxBoxSizer* list_sizer{nullptr};
        wxStaticText* info{nullptr};
        wxPoint scroll_pos{0, 0};
        wxStaticBitmap* filament_color_chip{nullptr};
        wxStaticText* title_label{nullptr};
        ::CheckBox* enable_check{nullptr};
        wxStaticText* enable_label{nullptr};
        wxSizerItem* full_line_item{nullptr};
        ::CheckBox* full_check{nullptr};
        wxStaticText* full_label{nullptr};
        bool is_mixed{false};
        std::string filament_type;
        std::string filament_vendor;
        std::string filament_id;
        size_t filament_slot{0};
        std::vector<Subcategory> subs;
        std::vector<size_t> rows;
    };

    struct MixedVisualSpec
    {
        bool valid{false};
        bool is_gradient{false};
        std::vector<wxColour> component_colours;
        std::vector<double> ratios;
        std::vector<double> tri_weights;
        std::vector<std::pair<double, double>> gradient_samples;
        std::vector<std::pair<double, double>> gradient_anchors;
    };

    struct SectionGroup
    {
        wxString title;
        Section kind{Section::Print};
        std::string icon_name;
        ScalableBitmap icon_bmp;
        wxPanel* page{nullptr};
        TabCtrl* tabs{nullptr};
        MultiSwitchButton* variant_switch{nullptr};
        TabCtrl* mixed_tabs{nullptr};
        wxPanel* page_host{nullptr};
        wxBoxSizer* page_host_sizer{nullptr};
        int selected_inner{-1};
        int selected_mixed{-1};
        std::vector<size_t> categories;
        std::vector<size_t> mixed_categories;
    };

    void build_option_model();
    void apply_selection(const std::vector<std::string>& published_keys,
                         const std::vector<Slic3r::PublishedMaterialEntry>& material_keys);
    static MixedVisualSpec make_mixed_visual_spec(const Slic3r::DynamicPrintConfig& full, size_t slot);
    void apply_filter(const wxString& filter_text);
    void apply_pseudo_filter(bool selected_only);
    void refresh_filter(const wxString& filter);
    void select_all(bool value);
    void select_visible(bool value);
    void show_menu(wxMouseEvent& evt);
    void set_row_bold(Row& row, bool bold);
    void on_full_toggle(size_t category_index);
    void on_enable_toggle(size_t category_index);
    bool category_has_selection(const Category& cat) const;
    void refresh_tab_indicators();
    std::vector<MixedDependencyIssue> unpublished_mixed_components() const;
    void add_mixed_visual(size_t category_index, const MixedVisualSpec& spec);
    size_t section_group_for(Section kind);
    size_t category_index_for(const wxString& title,
                              Section section,
                              size_t group,
                              size_t source_index,
                              const PublishMaterialIdentity& identity = PublishMaterialIdentity(),
                              bool is_mixed                          = false);
    size_t subcategory_index_for(size_t category_index, const wxString& title, const wxString& icon);
    void add_row_ui(const std::string& key,
                    const wxString& label,
                    const wxString& value,
                    const wxString& unit,
                    size_t category_index,
                    size_t subcategory_index,
                    RowKind kind = RowKind::Setting);
    std::vector<std::string> full_keys_for_slot() const;
    void save_scroll_position(Category& category);
    void show_outer_page(size_t section_index);
    void show_inner_page(size_t section_index, int inner_index);
    void show_mixed_page(size_t section_index, int mixed_index);
    void on_outer_tab_changed(wxCommandEvent& event);
    void on_inner_tab_changed(size_t section_index, wxCommandEvent& event);
    void on_mixed_tab_changed(size_t section_index, wxCommandEvent& event);
    bool row_is_visible(const Row& row) const;
    void apply_visibility();
    void bind_tab_events();
    void setup_variant_switch(size_t section_index);

    TabCtrl* m_outer_tabs{nullptr};
    wxPanel* m_outer_host{nullptr};
    wxBoxSizer* m_outer_host_sizer{nullptr};
    int m_selected_outer{-1};
    wxBoxSizer* m_fb_sizer{nullptr};
    enum class FilterMode { Text, SelectedOnly, UnselectedOnly };
    FilterMode m_filter_mode{FilterMode::Text};
    TextInput* m_filter_box{nullptr};
    wxTextCtrl* m_filter_ctrl{nullptr};
    wxStaticBitmap* m_menu_button{nullptr};
    wxStaticText* m_pseudo_chip{nullptr};
    wxString m_info_nonsel;
    wxString m_info_allsel;
    wxString m_info_empty;
    wxString m_info_mix;

    ScalableBitmap m_search;
    ScalableBitmap m_menu;

    std::vector<Row> m_rows;
    std::vector<Category> m_categories;
    std::vector<SectionGroup> m_sections;
};

}}
