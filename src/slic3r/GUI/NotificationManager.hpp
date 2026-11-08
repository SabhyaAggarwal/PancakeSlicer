#ifndef slic3r_GUI_NotificationManager_hpp_
#define slic3r_GUI_NotificationManager_hpp_

#include "GUI_App.hpp"
#include "Plater.hpp"
#include "GLCanvas3D.hpp"
#include "Event.hpp"
#include "I18N.hpp"
#include "Jobs/ProgressIndicator.hpp"
#include "Downloader.hpp"

#include <cstdint>
#include <cstddef>
#include <imgui.h>
#include <libslic3r/ObjectID.hpp>
#include "libslic3r/PrintBase.hpp"
#include <libslic3r/Technologies.hpp>

#include <wx/event.h>
#include <limits>
#include <memory>
#include <wx/time.h>

#include <string>
#include <functional>
#include <utility>
#include <vector>
#include <deque>
#include <unordered_set>

#include <libslic3r/Preset.hpp>
#include <wx/utils.h>

namespace Slic3r {
namespace GUI {

using EjectDriveNotificationClickedEvent = SimpleEvent;
wxDECLARE_EVENT(EVT_EJECT_DRIVE_NOTIFICAION_CLICKED, EjectDriveNotificationClickedEvent);
using ExportGcodeNotificationClickedEvent = SimpleEvent;
wxDECLARE_EVENT(EVT_EXPORT_GCODE_NOTIFICAION_CLICKED, ExportGcodeNotificationClickedEvent);
using PresetUpdateAvailableClickedEvent = SimpleEvent;
wxDECLARE_EVENT(EVT_PRESET_UPDATE_AVAILABLE_CLICKED, PresetUpdateAvailableClickedEvent);
using PrinterConfigUpdateAvailableClickedEvent = SimpleEvent;
wxDECLARE_EVENT(EVT_PRINTER_CONFIG_UPDATE_AVAILABLE_CLICKED, PrinterConfigUpdateAvailableClickedEvent);

using CancelFn = std::function<void()>;

class GLCanvas3D;
class ImGuiWrapper;
enum class InfoItemType;

#define BBL_NOTICE_PLATEINFO_OBJID      1024+1
#define BBL_NOTICE_OBJECTS_OBJID        1024+2

#define BBL_NOTICE_MAX_INTERVAL         86400 * 10

struct JumpTo
{
	std::string text;
	std::string opt;
	Preset::Type opt_type;
};

enum class NotificationType
{
	CustomNotification = 0,
	ExportFinished,
	Mouse3dDisconnected,
	NewAppAvailable,
	NewAlphaAvailable,
	NewBetaAvailable,
	PresetUpdateAvailable,
	PresetUpdateFinished,
	ValidateError,
	ValidateWarning,
	SlicingError,
    SlicingSeriousWarning,
	SlicingWarning,
    ClearanceWarning,
    NonplanarWarning,
	BBLGeneralError,
	PlaterError,
	LeftExtruderUnprintableError,
	RightExtruderUnprintableError,
	PlaterWarning,
	ProgressBar,
	PrintHostUpload,
	SlicingProgress,
    EmptyColorChangeCode,
    CustomSupportsAndSeamRemovedAfterRepair,
    EmptyAutoColorChange,
    SignDetected,
    QuitSLAManualMode,
    DesktopIntegrationSuccess,
    DesktopIntegrationFail,
    UndoDesktopIntegrationSuccess,
    UndoDesktopIntegrationFail,
    MmSegmentationExceededExtrudersLimit,
	DidYouKnowHint,
	UpdatedItemsInfo,
	ProgressIndicator,
	SimplifySuggestion,
	UnknownFont,
	CgalFinished,
	ExportOngoing,
    URLDownload,
	ArrangeOngoing,
	BBLPlateInfo,
	BBL3MFInfo,
	BBLObjectInfo,
	BBLSliceEmptyLayer,
	BBLNeedSupportON,
	BBLGcodeOverlap,
	BBLSeqPrintInfo,
	BBLPluginInstallHint,
    BBLFlushingVolumeZero,
    BBLMixedFilamentBroken,
	BBLPluginUpdateAvailable,
	BBLPreviewOnlyMode,
    BBLPrinterConfigUpdateAvailable,
	BBLUserPresetExceedLimit,
    BBLFilamentPrintableError,
    BBLSliceLimitError,
    BBLSliceMultiExtruderHeightOutside,
	BBLBedFilamentIncompatible,
    BBLMixUsePLAAndPETG,
	BBLNozzleFilamentIncompatible,
    BBLSingleExtruderMixedFilamentRisk,
    OrcaSharedProfilesAvailable,
	OrcaCloudAPIError,
    OrcaSyncConflict,
    OrcaLocalPluginMissingError,
    OrcaCloudPluginMissingError,
    OrcaPluginInactiveError,
    OrcaPluginCapabilityUnavailableError,
    NotificationTypeCount

};

class NotificationManager
{
public:
	enum class NotificationLevel : int
	{
		ProgressBarNotificationLevel = 1,
		HintNotificationLevel,
		RegularNotificationLevel,
		PrintInfoNotificationLevel,
		PrintInfoShortNotificationLevel,
		ImportantNotificationLevel,
		WarningNotificationLevel,
        SeriousWarningNotificationLevel,
		ErrorNotificationLevel,
	};

	NotificationManager(wxEvtHandler* evt_handler);
	~NotificationManager(){}

	void on_change_color_mode(bool is_dark);
	void init() { m_initialized = true; }
	void push_notification(const NotificationType type, int timestamp = 0);
	void push_notification(const std::string& text, int timestamp = 0);
    void push_notification(NotificationType type, NotificationLevel level, const std::string& text, const std::string& hypertext = "",
                           std::function<bool(wxEvtHandler*)> callback = std::function<bool(wxEvtHandler*)>(), int timestamp = 0);
	void push_delayed_notification(const NotificationType type, std::function<bool(void)> condition_callback, int64_t initial_delay, int64_t delay_interval);
	void stop_delayed_notifications_of_type(const NotificationType type);
	void push_validate_error_notification(StringObjectException const & error);
	void push_upload_job_notification(int id, float filesize, const std::string& filename, const std::string& host, float percentage = 0);
	void set_upload_job_notification_percentage(int id, const std::string& filename, const std::string& host, float percentage);
	void upload_job_notification_show_canceled(int id, const std::string& filename, const std::string& host);
	void upload_job_notification_show_error(int id, const std::string& filename, const std::string& host);
    void push_slicing_serious_warning_notification(const std::string &text, std::vector<ModelObject const *> objs);
    void push_slicing_serious_warning_notification(const std::string &text, std::vector<ModelInstance const *> insts);
    void close_slicing_serious_warning_notification(const std::string &text);
    void push_slicing_error_notification(const std::string &text, std::vector<ModelObject const *> objs);
    void push_slicing_warning_notification(const std::string &text, bool gray, ModelObject const *obj, ObjectID oid, int warning_step, int warning_msg_id, NotificationLevel level = NotificationLevel::WarningNotificationLevel);
	void set_all_slicing_errors_gray(bool g);
	void set_all_slicing_warnings_gray(bool g);
	void close_slicing_errors_and_warnings();
	void close_slicing_error_notification(const std::string& text);
	void remove_slicing_warnings_of_released_objects(const std::vector<ObjectID>& living_oids);
	void push_general_error_notification(const std::string& text);
	void close_general_error_notification(const std::string& text);
	void push_plater_error_notification(const std::string& text);
	void push_plater_warning_notification(const std::string& text);
	void close_plater_error_notification(const std::string& text);
	void close_plater_warning_notification(const std::string& text);
    void push_flushing_volume_error_notification(NotificationType type, NotificationLevel level, const std::string &text, const std::string &hypertext = "", std::function<bool(wxEvtHandler *)> callback  = std::function<bool(wxEvtHandler *)>());
    void close_flushing_volume_error_notification(NotificationType type, NotificationLevel level);
    void push_slicing_customize_error_notification(NotificationType type, NotificationLevel level, const std::string &text, const std::string &hypertext = "", std::function<bool(wxEvtHandler*)> callback = std::function<bool(wxEvtHandler*)>());
    void close_slicing_customize_error_notification(NotificationType type, NotificationLevel level);

	void push_simplify_suggestion_notification(const std::string& text, ObjectID object_id, const std::string& hypertext = "",
		std::function<bool(wxEvtHandler*)> callback = std::function<bool(wxEvtHandler*)>());
    void set_simplify_suggestion_multiline(const ObjectID oid, bool bMulti);
	void remove_simplify_suggestion_of_released_objects(const std::vector<ObjectID>& living_oids);
	void remove_simplify_suggestion_with_id(const ObjectID oid);
	void set_sidebar_collapsed(bool collapsed);
	void set_fff(bool b);
	void set_fdm(bool b) { set_fff(b); }
	void set_sla(bool b) { set_fff(!b); }
	void push_exporting_finished_notification(const std::string& path, const std::string& dir_path, bool on_removable);
	void push_import_finished_notification(const std::string& path, const std::string& dir_path, bool on_removable);

	void push_shared_profiles_notification(const std::string& explore_url);
	void push_orca_sync_conflict_notification(const std::string& text,
		int conflict_code,
		std::function<bool(wxEvtHandler*)> pull_callback,
		std::function<bool(wxEvtHandler*)> force_push_callback);
	void push_plugin_missing_notification(NotificationType type, const std::string& text,
		const std::string& resolve_label,
		std::vector<JumpTo> body,
		std::function<bool(wxEvtHandler*)> resolve_callback);

    void push_download_URL_progress_notification(size_t id, const std::string& text, std::function<bool(DownloaderUserAction, int)> user_action_callback);
    void set_download_URL_progress(size_t id, float percentage);
    void set_download_URL_paused(size_t id);
    void set_download_URL_canceled(size_t id);
    void set_download_URL_error(size_t id, const std::string& text);

	void init_slicing_progress_notification(std::function<bool()> cancel_callback);
	void update_slicing_notif_dailytips(bool need_change);
	void set_slicing_progress_began();
	void set_slicing_progress_percentage(const std::string& text, float percentage);
	void set_slicing_progress_canceled(const std::string& text);
	void set_slicing_progress_hidden();
	void set_slicing_complete_print_time(const std::string& info, bool sidebar_colapsed);
	void set_slicing_progress_export_possible();
	void init_progress_indicator();
	void progress_indicator_set_range(int range);
	void progress_indicator_set_cancel_callback(CancelFn callback = CancelFn());
	void progress_indicator_set_progress(int pr);
	void progress_indicator_set_status_text(const char*);
	int  progress_indicator_get_range() const;
	void push_hint_notification(bool open_next);
	bool is_hint_notification_open();
	void deactivate_loaded_hints();
	void push_updated_item_info_notification(InfoItemType type);
	void new_export_began(bool on_removable);
	void device_ejected();
    void render_notifications(GLCanvas3D &canvas, float overlay_width, float bottom_margin, float right_margin);
	void close_notification_of_type(const NotificationType type);
    void remove_notification_of_type(const NotificationType type);
    void clear_all();
    void set_in_preview(bool preview);
	void apply_in_preview() { set_in_preview(m_in_preview); }
	void set_move_from_overlay(bool move) { m_move_from_overlay = move; }
	bool update_notifications(GLCanvas3D& canvas);
	size_t get_notification_count() const;


    void bbl_show_plateinfo_notification(const std::string &text);
    void bbl_close_plateinfo_notification();

    void bbl_show_3mf_warn_notification(const std::string &text, NotificationLevel level = NotificationLevel::ErrorNotificationLevel);
    void bbl_close_3mf_warn_notification();

    void bbl_show_preview_only_notification(const std::string &text);
    void bbl_close_preview_only_notification();

    void bbl_show_plugin_install_notification(const std::string &text);
    void bbl_close_plugin_install_notification();

	void bbl_show_objectsinfo_notification(const std::string &text, bool is_warning, bool is_hidden);
    void bbl_close_objectsinfo_notification();

    void bbl_show_seqprintinfo_notification(const std::string &text);
    void bbl_close_seqprintinfo_notification();

	void bbl_show_slice_emptylayer_notification(const std::string &text, bool bOverride = true);

	void bbl_show_app_newversion_notification();

	void bbl_show_need_support_on_notification();
    void bbl_close_need_support_on_notification();

    void bbl_show_gcode_overlap_notification();
    void bbl_close_gcode_overlap_notification();

	void bbl_show_bed_filament_incompatible_notification(const std::string& text);
	void bbl_close_bed_filament_incompatible_notification();

	void bbl_show_filament_map_invalid_notification_before_slice(const NotificationType type, const std::string& text);
	void bbl_close_filament_map_invalid_notification_before_slice(const NotificationType type);

	void bbl_show_filament_map_invalid_notification_after_slice(const NotificationType type, const std::string& text);
	void bbl_close_filament_map_invalid_notification_after_slice(const NotificationType type);

    void bbl_show_sole_text_notification(NotificationType sType,const std::string &text, bool bOverride, int level, bool autohide);
    void bbl_chose_sole_text_notification(NotificationType sType);

private:
	struct NotificationData {
		NotificationType         type;
		NotificationLevel        level;
		const int                duration;
		const std::string        text1;
		const std::string        hypertext;
        std::function<bool(wxEvtHandler*)> callback;
		const std::string        text2;
		int                      sub_msg_id {-1};
		std::string        ori_text;
        bool                use_warn_color { false };
	};

	class NotificationIDProvider
	{
	public:
		int 		allocate_id();
		void 		release_id(int id);

	private:
		int       			m_next_id{ 1 };
		std::vector<int>	m_released_ids;
	};

	class PopNotification
	{
	public:

		enum class EState
		{
			Unknown,
			Hidden,
			Shown,
			NotFading,
			FadingOut,
			ClosePending,
			Finished,
			Hovered,
			Paused
		};

		PopNotification(const NotificationData &n, NotificationIDProvider &id_provider, wxEvtHandler* evt_handler);
		virtual ~PopNotification() { if (m_id) m_id_provider.release_id(m_id); }
		virtual void           render(GLCanvas3D& canvas, float initial_y, bool move_from_overlay, float overlay_width, float right_margin);
        virtual void bbl_render_block_notification(GLCanvas3D &canvas, float initial_y, bool move_from_overlay, float overlay_width, float right_margin);
        virtual void close();
		void                   update(const NotificationData& n);
		void                   append(const std::string& append_str);
		bool                   is_finished() const { return m_state == EState::ClosePending || m_state == EState::Finished; }
        void                   reinit() { m_state = EState::Unknown; }
		float                  get_top() const { return m_top_y; }
		float                  get_bottom() const { return m_bottom_y; }
		float                  get_current_top() const { return m_top_y; }
		const NotificationType get_type() const { return m_data.type; }
		const NotificationData& get_data() const { return m_data; }
        std::string             get_text1() const { return m_text1; }
		const bool             is_gray() const { return m_is_gray; }
		void                   set_gray(bool g) { m_is_gray = g; }
		virtual bool           compare_text(const std::string& text) const;
        void                    hide(bool h);
		virtual bool           update_state(bool paused, const int64_t delta);
		int64_t 		       next_render() const { return is_finished() ? 0 : m_next_render; }
		EState                 get_state()  const { return m_state; }
		bool				   is_hovered() const { return m_state == EState::Hovered; }
		void				   set_hovered() { if (m_state != EState::Finished && m_state != EState::ClosePending && m_state != EState::Hidden && m_state != EState::Unknown) m_state = EState::Hovered; }
		void                   reset_timer() { m_notification_start = GLCanvas3D::timestamp_now(); m_state = EState::Shown; }
        void set_Multiline(bool Multi) { m_multiline = Multi; }
		virtual void on_change_color_mode(bool is_dark);
		void set_scale(float scale) { m_scale = scale; }
        typedef std::function<void(PopNotification*)> DeleteCallback;
        void set_delete_callback(DeleteCallback);
        bool is_valid_delete_callback();
	protected:
		virtual void init();
		virtual void set_next_window_size(ImGuiWrapper& imgui);
		virtual void render_text(ImGuiWrapper& imgui,
			                     const float win_size_x, const float win_size_y,
			                     const float win_pos_x , const float win_pos_y);
		virtual void render_close_button(ImGuiWrapper& imgui,
			                             const float win_size_x, const float win_size_y,
			                             const float win_pos_x , const float win_pos_y);
		virtual void render_hypertext(ImGuiWrapper& imgui,
			                          const float text_x, const float text_y,
		                              const std::string text,
		                              bool more = false);
		void render_hyperlink_action(ImGuiWrapper& imgui, float text_x, float text_y,
		                             const std::string& text, const char* button_id,
		                             const std::function<void()>& on_click);
		virtual void bbl_render_block_notif_text(ImGuiWrapper& imgui,
			const float win_size_x, const float win_size_y,
			const float win_pos_x, const float win_pos_y);
		virtual void bbl_render_block_notif_buttons(ImGuiWrapper& imgui,
			ImVec2 win_size,
			ImVec2 win_pos);
        virtual void bbl_render_block_notif_left_sign(ImGuiWrapper &imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y);
        virtual void bbl_render_left_sign(ImGuiWrapper &imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y);

		virtual void render_left_sign(ImGuiWrapper& imgui);
		virtual void render_minimize_button(ImGuiWrapper& imgui,
			                                const float win_pos_x, const float win_pos_y);
		virtual bool on_text_click();

		virtual void count_spaces();
		virtual void count_lines();
		virtual bool push_background_color();
		virtual int  get_duration() { return m_data.duration; }

		void ensure_ui_inited();

		bool m_is_dark = false;
        bool m_is_dark_inited = false;

		const NotificationData m_data;
		NotificationIDProvider &m_id_provider;
		int              m_id{ 0 };

        struct ImGUITheme
        {
            ImVec2 mWindowPadding;
            ImVec4 mWindowBkg;
            ImVec4 mBorderColor;
            ImVec4 mTextColor;
            float  mWindowRound;
        };

	    ImGUITheme m_DefaultTheme;

		ImVec4     m_WindowBkgColor;
        ImVec4     m_TextColor;

		ImVec4     m_HyperTextColor;
		ImVec4     m_HyperTextColorHover;

        ImVec4     m_ErrorColor;
        ImVec4     m_WarnColor;
        ImVec4     m_NormalColor;
		ImVec4     m_CurrentColor;

        float      m_WindowRadius;
        bool       m_WindowRadius_inited = false;

		void use_bbl_theme();
        void restore_default_theme();

		EState           m_state                { EState::Unknown };

		int64_t		 	 m_fading_start{ 0LL };

		int64_t		 	 m_notification_start;
		int64_t          m_next_render{ std::numeric_limits<int64_t>::max() };
		float            m_current_fade_opacity{ 1.0f };

		std::string      m_text1;
		std::string      m_hypertext;
		std::string      m_text2;
		size_t           pos_start = std::string::npos;
		size_t	         pos_end = std::string::npos;
		std::string      error_start = "<Error>";
		std::string      error_end = "</Error>";

        DeleteCallback m_on_delete_callback;
		float            m_window_width_offset;
		float            m_left_indentation;
		float            m_window_height        { 56.0f };
		float            m_window_width         { 450.0f };
		float            m_top_y                { 0.0f };
		float            m_bottom_y				{ 0.0f };
		float            m_line_height;
        std::vector<size_t> m_endlines;
		std::vector<size_t> m_endlines2;
		bool             m_is_gray              { false };
		bool             m_multiline            { false };
		bool             m_minimize_b_visible   { false };
        size_t           m_lines_count{ 1 };
		wxEvtHandler*    m_evt_handler;

		float m_scale = 1.0f;
	};

    void close_and_delete_self(PopNotification*);

	class ObjectIDNotification : public PopNotification
	{
	public:
		ObjectIDNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler)
			: PopNotification(n, id_provider, evt_handler)
		{}
		ObjectID 	object_id;
		int    		warning_step { 0 };
	};

	class PlaterWarningNotification : public PopNotification
	{
	public:
		PlaterWarningNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler) : PopNotification(n, id_provider, evt_handler) {}
        void close() override;
		void		 real_close()      { m_state = EState::ClosePending; wxGetApp().plater()->get_current_canvas3D()->schedule_extra_frame(0); }
		void         show()            { m_state = EState::Unknown; }
	};


	class ProgressBarNotification : public PopNotification
	{
	public:

		ProgressBarNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler) : PopNotification(n, id_provider, evt_handler) { }
		virtual void set_percentage(float percent) { m_percentage = percent; }
        float get_percentage() const { return m_percentage; }
	protected:
		virtual void init() override;
		virtual void	render_text(ImGuiWrapper& imgui,
									const float win_size_x, const float win_size_y,
									const float win_pos_x, const float win_pos_y) override;
		virtual void	render_bar(ImGuiWrapper& imgui,
									const float win_size_x, const float win_size_y,
									const float win_pos_x, const float win_pos_y) ;
		virtual void	render_cancel_button(ImGuiWrapper& imgui,
									const float win_size_x, const float win_size_y,
									const float win_pos_x, const float win_pos_y)
		{}
		void			render_minimize_button(ImGuiWrapper& imgui,
			const float win_pos_x, const float win_pos_y) override {}
		float				m_percentage {0.0f};

		bool				m_has_cancel_button {false};
		bool                m_render_percentage {false};

	};

    class URLDownloadNotification : public ProgressBarNotification
    {
    public:
        URLDownloadNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler, size_t download_id, std::function<bool(DownloaderUserAction, int)> user_action_callback)
                : ProgressBarNotification(n, id_provider, evt_handler)
                , m_download_id(download_id)
                , m_user_action_callback(user_action_callback)
        {
        }
        void	set_percentage(float percent) override
        {
            m_percentage = percent;
            if (m_percentage >= 1.f) {
                m_notification_start = GLCanvas3D::timestamp_now();
                m_state = EState::Shown;
            } else
                m_state = EState::NotFading;
        }
        size_t	get_download_id() { return m_download_id; }
        void	set_user_action_callback(std::function<bool(DownloaderUserAction, int)> user_action_callback) { m_user_action_callback = user_action_callback; }
        void	set_paused(bool paused) { m_download_paused = paused; }
        void    set_error_message(const std::string& message) { m_error_message = message; }
        bool    compare_text(const std::string& text) const override { return false; };
    protected:
        void	render_close_button(ImGuiWrapper& imgui,
                                    const float win_size_x, const float win_size_y,
                                    const float win_pos_x, const float win_pos_y) override;
        void    render_close_button_inner(ImGuiWrapper& imgui,
                                          const float win_size_x, const float win_size_y,
                                          const float win_pos_x, const float win_pos_y);
        void    render_pause_cancel_buttons_inner(ImGuiWrapper& imgui,
                                                  const float win_size_x, const float win_size_y,
                                                  const float win_pos_x, const float win_pos_y);
        void    render_open_button_inner(ImGuiWrapper& imgui,
                                         const float win_size_x, const float win_size_y,
                                         const float win_pos_x, const float win_pos_y);
        void    render_cancel_button_inner(ImGuiWrapper& imgui,
                                           const float win_size_x, const float win_size_y,
                                           const float win_pos_x, const float win_pos_y);
        void    render_pause_button_inner(ImGuiWrapper& imgui,
                                          const float win_size_x, const float win_size_y,
                                          const float win_pos_x, const float win_pos_y);
        void	render_bar(ImGuiWrapper& imgui,
                           const float win_size_x, const float win_size_y,
                           const float win_pos_x, const float win_pos_y) override;
        void    trigger_user_action_callback(DownloaderUserAction action);

        void    count_spaces() override;

        size_t							m_download_id;
        std::function<bool(DownloaderUserAction, int)>	m_user_action_callback;
        bool							m_download_paused {false};
        std::string						m_error_message;
    };

	class PrintHostUploadNotification : public ProgressBarNotification
	{
	public:
		enum class UploadJobState
		{
			PB_PROGRESS,
			PB_ERROR,
			PB_CANCELLED,
			PB_COMPLETED
		};
		PrintHostUploadNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler, float percentage, int job_id, float filesize)
			:ProgressBarNotification(n, id_provider, evt_handler)
			, m_job_id(job_id)
			, m_file_size(filesize)
		{
			m_has_cancel_button = true;
			set_percentage(percentage);
		}
		static std::string	get_upload_job_text(int id, const std::string& filename, const std::string& host) { return filename + " -> " + host; }
		void				set_percentage(float percent) override;
		void				cancel() { m_uj_state = UploadJobState::PB_CANCELLED; m_has_cancel_button = false; }
		void				error()  { m_uj_state = UploadJobState::PB_ERROR;     m_has_cancel_button = false; init(); }
		bool				compare_job_id(const int other_id) const { return m_job_id == other_id; }
		bool				compare_text(const std::string& text) const override { return false; }
	protected:
		void        init() override;
		void		count_spaces() override;
		bool		push_background_color() override;
		void		render_bar(ImGuiWrapper& imgui,
								const float win_size_x, const float win_size_y,
								const float win_pos_x, const float win_pos_y) override;
		void		render_cancel_button(ImGuiWrapper& imgui,
											const float win_size_x, const float win_size_y,
											const float win_pos_x, const float win_pos_y) override;
		void		render_left_sign(ImGuiWrapper& imgui) override;
		int					m_job_id;
		float			    m_file_size;
		long				m_hover_time{ 0 };
		UploadJobState		m_uj_state{ UploadJobState::PB_PROGRESS };
	};

	class ProgressIndicatorNotification : public ProgressBarNotification
	{
	public:
		enum class ProgressIndicatorState
		{
			PIS_HIDDEN,
			PIS_PROGRESS_REQUEST,
			PIS_PROGRESS_UPDATED,
			PIS_COMPLETED
		};
		ProgressIndicatorNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler)
		: ProgressBarNotification(n, id_provider, evt_handler)
		{
			m_render_percentage = true;
		}
		void set_range(int range) { m_range = range; }
		void set_cancel_callback(CancelFn callback) { m_cancel_callback = callback; }
		void set_progress(int pr) { set_percentage((float)pr / (float)m_range); }
		void set_status_text(const char*);
		int  get_range() const { return m_range; }
		void init() override;
		void set_percentage(float percent) override;
		bool update_state(bool paused, const int64_t delta) override;
	protected:
		int						m_range { 100 };
		CancelFn				m_cancel_callback { nullptr };
		ProgressIndicatorState  m_progress_state { ProgressIndicatorState::PIS_HIDDEN };

		void		render_close_button(ImGuiWrapper& imgui,
			                            const float win_size_x, const float win_size_y,
			                            const float win_pos_x, const float win_pos_y) override;
		void		render_cancel_button(ImGuiWrapper& imgui,
									     const float win_size_x, const float win_size_y,
									     const float win_pos_x, const float win_pos_y) override;
		void        on_cancel_button() { if (m_cancel_callback) m_cancel_callback(); }
	};

	class ExportFinishedNotification : public PopNotification
	{
	public:
		ExportFinishedNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler, bool to_removable,const std::string& export_path,const std::string& export_dir_path)
			: PopNotification(n, id_provider, evt_handler)
			, m_to_removable(to_removable)
			, m_export_path(export_path)
			, m_export_dir_path(export_dir_path)
		    {
				m_multiline = true;
			}
		bool        m_to_removable;
		std::string m_export_path;
		std::string m_export_dir_path;
	protected:
		void count_spaces() override;
		void render_text(ImGuiWrapper& imgui,
						 const float win_size_x, const float win_size_y,
						 const float win_pos_x, const float win_pos_y) override;
		void render_close_button(ImGuiWrapper& imgui,
								 const float win_size_x, const float win_size_y,
								 const float win_pos_x, const float win_pos_y) override;
		void render_eject_button(ImGuiWrapper& imgui,
			                             const float win_size_x, const float win_size_y,
			                             const float win_pos_x, const float win_pos_y);
		void render_minimize_button(ImGuiWrapper& imgui, const float win_pos_x, const float win_pos_y) override
			{ m_minimize_b_visible = false; }
		bool on_text_click() override;
		void on_eject_click();
		long      m_hover_time { 0 };
		bool	  m_eject_pending { false };
	};

	class UpdatedItemsInfoNotification : public PopNotification
	{
	public:
		UpdatedItemsInfoNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler, InfoItemType info_item_type)
			: PopNotification(n, id_provider, evt_handler)
		{
		}
		void count_spaces() override;
		void add_type(InfoItemType type);
		void close() override{
			for (auto& tac : m_types_and_counts)
				tac.second = 0;
			PopNotification::close();
		}
	protected:
		std::vector<std::pair<InfoItemType, size_t>> m_types_and_counts;
	};

	class SharedProfilesNotification : public PopNotification
	{
	public:
		SharedProfilesNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler,
			const std::string& explore_url)
			: PopNotification(n, id_provider, evt_handler)
			, m_explore_url(explore_url)
		{
			m_multiline = true;
		}
	protected:
		void init() override;
		void render_text(ImGuiWrapper& imgui,
			const float win_size_x, const float win_size_y,
			const float win_pos_x, const float win_pos_y) override;
		bool on_text_click() override;
		void render_hypertext(ImGuiWrapper& imgui,
			const float text_x, const float text_y,
			const std::string text, bool more = false) override;

		std::string m_explore_url;
		bool m_dont_show_clicked{ false };
	};

	class OrcaSyncConflictNotification : public PopNotification
	{
	public:
		OrcaSyncConflictNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler,
			std::function<bool(wxEvtHandler*)> pull_callback,
			std::function<bool(wxEvtHandler*)> force_push_callback,
			int conflict_code)
			: PopNotification(n, id_provider, evt_handler)
			, m_pull_callback(std::move(pull_callback))
			, m_force_push_callback(std::move(force_push_callback))
			, conflict_code(conflict_code)
		{
			m_multiline = true;
		}
	protected:
		void init() override;
		void render_text(ImGuiWrapper& imgui,
			const float win_size_x, const float win_size_y,
			const float win_pos_x, const float win_pos_y) override;

		std::function<bool(wxEvtHandler*)> m_pull_callback;
		std::function<bool(wxEvtHandler*)> m_force_push_callback;
		int conflict_code;
	};

	class PluginMissingNotification : public PopNotification
	{
	public:
		PluginMissingNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler,
			std::string resolve_label,
			std::vector<JumpTo> body,
			std::function<bool(wxEvtHandler*)> resolve_callback)
			: PopNotification(n, id_provider, evt_handler)
			, m_resolve_label(std::move(resolve_label))
			, m_body(std::move(body))
			, m_resolve_callback(std::move(resolve_callback))
		{
			m_multiline = true;
		}
    protected:
		void init() override;
		void render_text(ImGuiWrapper& imgui,
			const float win_size_x, const float win_size_y,
			const float win_pos_x, const float win_pos_y) override;
		void render_close_button(ImGuiWrapper& ,
			const float , const float ,
			const float , const float ) override {}
		void render_minimize_button(ImGuiWrapper& ,
			const float , const float ) override { m_minimize_b_visible = false; }
		void bbl_render_block_notif_text(ImGuiWrapper& imgui,
			const float win_size_x, const float win_size_y,
			const float win_pos_x, const float win_pos_y) override;
		void bbl_render_block_notif_buttons(ImGuiWrapper& ,
			ImVec2 , ImVec2 ) override {}

		std::string m_resolve_label;
		std::vector<JumpTo> m_body;
		std::function<bool(wxEvtHandler*)> m_resolve_callback;
	};

	class SlicingProgressNotification;

	class HintNotification;

	struct DelayedNotification
	{
		std::unique_ptr<PopNotification>	notification;
		std::function<bool(void)>           condition_callback;
		int64_t								remaining_time;
		int64_t                             delay_interval;

		DelayedNotification(std::unique_ptr<PopNotification> n, std::function<bool(void)> cb, int64_t r, int64_t d)
		: notification(std::move(n))
	    , condition_callback(cb)
		, remaining_time(r)
		, delay_interval(d)
		{}
	};

	bool push_notification_data(const NotificationData& notification_data, int timestamp);
	bool push_notification_data(std::unique_ptr<NotificationManager::PopNotification> notification, int timestamp);
	void push_delayed_notification_data(std::unique_ptr<NotificationManager::PopNotification> notification, std::function<bool(void)> condition_callback, int64_t initial_delay, int64_t delay_interval);
	bool activate_existing(const NotificationManager::PopNotification* notification);
	void sort_notifications();
    bool has_slicing_error_notification();
	size_t get_standard_duration(NotificationLevel level)
	{
		switch (level) {

		case NotificationLevel::ErrorNotificationLevel: 			return 0;
		case NotificationLevel::WarningNotificationLevel:			return 0;
		case NotificationLevel::ImportantNotificationLevel:			return 20;
		case NotificationLevel::ProgressBarNotificationLevel:		return 2;
		case NotificationLevel::PrintInfoShortNotificationLevel:	return 5;
		case NotificationLevel::RegularNotificationLevel: 			return 10;
		case NotificationLevel::PrintInfoNotificationLevel:			return 10;
		case NotificationLevel::HintNotificationLevel:				return 300;
		default: return 10;
		}
	}

	bool m_is_dark = false;
	bool m_initialized{ false };
	bool m_imgui_ready{ false };
	wxEvtHandler*                m_evt_handler;
	NotificationIDProvider 		 m_id_provider;
	std::deque<std::unique_ptr<PopNotification>> m_pop_notifications;
    PopNotification* m_to_delete_after_finish_render{nullptr};
	std::vector<DelayedNotification> m_waiting_notifications;
	std::unordered_set<int>      m_used_timestamps;
	bool                         m_in_preview { false };
	int                          m_in_view{ 0 };
	bool                         m_move_from_overlay { false };
	int64_t						 m_last_render { 0LL };
	const std::vector<NotificationType> m_multiple_types = {
		NotificationType::CustomNotification,
		NotificationType::PlaterWarning,
		NotificationType::ProgressBar,
		NotificationType::PrintHostUpload,
        NotificationType::SimplifySuggestion,
        NotificationType::ValidateWarning,
        NotificationType::NonplanarWarning,
        NotificationType::BBL3MFInfo
	};
	const std::vector<NotificationData> basic_notifications = {
        NotificationData{NotificationType::Mouse3dDisconnected, NotificationLevel::RegularNotificationLevel, 10, _u8L("3D Mouse disconnected.")},
        NotificationData{NotificationType::PresetUpdateAvailable, NotificationLevel::ImportantNotificationLevel, BBL_NOTICE_MAX_INTERVAL, _u8L("A new configuration is available. Update now\?"), _u8L("More"),
		[](wxEvtHandler* evnthndlr) {
			if (evnthndlr != nullptr)
				wxPostEvent(evnthndlr, PresetUpdateAvailableClickedEvent(EVT_PRESET_UPDATE_AVAILABLE_CLICKED));
			return true;
		}
	},
        NotificationData{NotificationType::EmptyColorChangeCode, NotificationLevel::PrintInfoNotificationLevel, 10,
         std::string(_devL("value can not be empty when you add a G-code for color change.\nPlease check the \"Color Change G-code\" in \"Printer Settings > Custom G-code\"").mb_str())},
        NotificationData{NotificationType::EmptyAutoColorChange, NotificationLevel::PrintInfoNotificationLevel, 10,
                         std::string(_devL("You need add a color change event to the print. The print need look like a sign.").mb_str())},
        NotificationData{NotificationType::DesktopIntegrationSuccess, NotificationLevel::RegularNotificationLevel, 10,
		_u8L("Integration was successful.") },
        NotificationData{NotificationType::DesktopIntegrationFail, NotificationLevel::WarningNotificationLevel, 10,
		_u8L("Integration failed.") },
        NotificationData{NotificationType::UndoDesktopIntegrationSuccess, NotificationLevel::RegularNotificationLevel, 10,
		_u8L("Undo integration was successful.") },

        NotificationData{NotificationType::BBLPluginUpdateAvailable, NotificationLevel::ImportantNotificationLevel, BBL_NOTICE_MAX_INTERVAL,
			_u8L("New network plug-in available."),
			_u8L("Details"),
                         [](wxEvtHandler* evnthndlr) {
                 wxCommandEvent* evt = new wxCommandEvent(EVT_UPDATE_PLUGINS_WHEN_LAUNCH);
				 wxQueueEvent(wxGetApp().plater(), evt);
				 return true;
             }},

        NotificationData{NotificationType::BBLPrinterConfigUpdateAvailable, NotificationLevel::ImportantNotificationLevel, BBL_NOTICE_MAX_INTERVAL,
                         _u8L("New printer config available."), _u8L("Details"),
                         [](wxEvtHandler *evnthndlr) {
                             if (evnthndlr != nullptr) wxPostEvent(evnthndlr, PrinterConfigUpdateAvailableClickedEvent(EVT_PRINTER_CONFIG_UPDATE_AVAILABLE_CLICKED));
                             return true;
                         }},

        NotificationData{NotificationType::BBLUserPresetExceedLimit, NotificationLevel::WarningNotificationLevel, BBL_NOTICE_MAX_INTERVAL,
			_u8L("The number of user presets cached in the cloud has exceeded the upper limit, newly created user presets can only be used locally."), 
			_u8L("Wiki Guide"),
                         [](wxEvtHandler* evnthndlr) {
				wxLaunchDefaultBrowser("https://wiki.bambulab.com/en/software/bambu-studio/3rd-party-printer-profile#cloud-user-presets-limit");
				return false;
             }},

        NotificationData{NotificationType::UndoDesktopIntegrationFail, NotificationLevel::WarningNotificationLevel, 10,
		_u8L("Undo integration failed.") },
        NotificationData{NotificationType::ExportOngoing, NotificationLevel::RegularNotificationLevel, 0, _u8L("Exporting")},
        NotificationData{NotificationType::NewAppAvailable, NotificationLevel::ImportantNotificationLevel, 20, _u8L("An update is available!"), _u8L("Go to download page"),
                         [](wxEvtHandler *evnthndlr) {
				wxGetApp().open_browser_with_warning_dialog("https://www.thingiverse.com/"); return true;
			 }},
	};
public:
    void set_scale(float scale = 1.0);
	float m_scale = 1.0f;
};

}
}

#endif
