#ifndef slic3r_GUI_SlicingProgressNotification_hpp_
#define slic3r_GUI_SlicingProgressNotification_hpp_

#include "DailyTips.hpp"
#include "NotificationManager.hpp"
#include <wx/event.h>
#include <functional>
#include <string>
#include <cstdint>
#include <imgui.h>

namespace Slic3r { namespace GUI {


class NotificationManager::SlicingProgressNotification : public NotificationManager::PopNotification
{
public:
    enum class SlicingProgressState
    {
        SP_NO_SLICING,
        SP_BEGAN,
        SP_PROGRESS,
        SP_CANCELLED,
        SP_COMPLETED
    };
    SlicingProgressNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler, std::function<bool()> callback)
        : PopNotification(n, id_provider, evt_handler)
        , m_cancel_callback(callback)
        , m_dailytips_panel(new DailyTipsPanel(true, DailyTipsLayout::Vertical))
    {
        set_progress_state(SlicingProgressState::SP_NO_SLICING);
    }
    void                set_percentage(float percent) { m_percentage = percent; }
    DailyTipsPanel*     get_dailytips_panel() { return m_dailytips_panel; }
    SlicingProgressState get_progress_state() { return m_sp_state; }
    void				set_status_text(const std::string& text);
    void			    set_cancel_callback(std::function<bool()> callback) { m_cancel_callback = callback; }
    bool                has_cancel_callback() const { return m_cancel_callback != nullptr; }
    bool				set_progress_state(float percent);
    bool				set_progress_state(SlicingProgressState state, float percent = 0.f);
    void			    set_print_info(const std::string& info);
    void                set_sidebar_collapsed(bool collapsed);
    bool                update_state(bool paused, const int64_t delta) override;
    void				set_fff(bool b) { m_is_fff = b; }
    void                set_export_possible(bool b) { m_export_possible = b; }
    void                on_change_color_mode(bool is_dark) override;
protected:
    void        init() override;
    void        render(GLCanvas3D& canvas, float initial_y, bool move_from_overlay, float overlay_width, float right_margin) override;
    void	    render_progress_text(const ImVec2& pos);
    void		render_bar(const ImVec2& pos, const ImVec2& size);
    void		render_cancel_button(const ImVec2& pos, const ImVec2& size);
    void		render_progress_close_button(const ImVec2& pos, const ImVec2& size);
    void        render_dailytips_panel(const ImVec2& pos, const ImVec2& size);
    void        render_show_dailytips(const ImVec2& pos);

    void        on_show_dailytips();
    void        on_cancel_button();
    int		    get_duration() override;

protected:
    ImVec2                  m_window_pos;
    float                   m_percentage{ 0.0f };
    int64_t                 m_before_complete_start;
    std::function<bool()>	m_cancel_callback;
    SlicingProgressState	m_sp_state{ SlicingProgressState::SP_PROGRESS };
    bool                    m_sidebar_collapsed{ false };
    bool                    m_export_possible{ false };
    DailyTipsPanel*         m_dailytips_panel{ nullptr };

    bool				    m_has_print_info{ false };
    std::string             m_print_info;
    bool					m_is_fff{ true };
};

}}

#endif