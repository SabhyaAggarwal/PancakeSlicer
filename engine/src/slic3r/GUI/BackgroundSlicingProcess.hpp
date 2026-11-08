#ifndef slic3r_GUI_BackgroundSlicingProcess_hpp_
#define slic3r_GUI_BackgroundSlicingProcess_hpp_

#include <exception>
#include <cassert>
#include <cstddef>
#include "libslic3r/Config.hpp"
#include "libslic3r/Polygon.hpp"
#include <memory>
#include <functional>
#include <string>
#include <condition_variable>
#include <mutex>

#include <boost/thread.hpp>

#include <utility>
#include <vector>
#include <wx/event.h>

#include "libslic3r/PrintBase.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/Format/SL1.hpp"
#include "slic3r/Utils/PrintHost.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"

namespace Slic3r { class Polygon; }
namespace Slic3r { class Print; }
namespace Slic3r::GUI { class PartPlate; }
namespace Slic3r::GUI { class Plater; }

namespace boost { namespace filesystem { class path; } }

namespace Slic3r {

class DynamicPrintConfig;
class Model;
class SLAPrint;

class SlicingStatusEvent : public wxEvent
{
public:
	SlicingStatusEvent(wxEventType eventType, int winid, const PrintBase::SlicingStatus &status) :
		wxEvent(winid, eventType), status(std::move(status)) {}
	virtual wxEvent *Clone() const { return new SlicingStatusEvent(*this); }

	PrintBase::SlicingStatus status;
};

class SlicingProcessCompletedEvent : public wxEvent
{
public:
	enum StatusType {
		Finished,
		Cancelled,
		Error
	};

	SlicingProcessCompletedEvent(wxEventType eventType, int winid, StatusType status, std::exception_ptr exception) :
		wxEvent(winid, eventType), m_status(status), m_exception(exception) {}
	virtual wxEvent* Clone() const { return new SlicingProcessCompletedEvent(*this); }

	StatusType 	status()    const { return m_status; }
	bool 		finished()  const { return m_status == Finished; }
	bool 		success()   const { return m_status == Finished; }
	bool 		cancelled() const { return m_status == Cancelled; }
	bool		error() 	const { return m_status == Error; }
	bool 		critical_error() const;
	bool        invalidate_plater() const;
	void 		rethrow_exception() const { assert(this->error()); assert(m_exception); std::rethrow_exception(m_exception); }
    std::pair<std::string, std::vector<size_t>> format_error_message() const;

private:
	StatusType 			m_status;
	std::exception_ptr 	m_exception;
};

enum BackgroundSlicingProcessStep {
    bspsGCodeFinalize, bspsCount,
};

class BackgroundSlicingProcess
{
public:
	BackgroundSlicingProcess();
	~BackgroundSlicingProcess();

	void set_fff_print(Print *print) { m_fff_print = print; }
    void set_sla_print(SLAPrint *print) { m_sla_print = print; m_sla_print->set_printer(&m_sla_archive); }
	void set_thumbnail_cb(ThumbnailsGeneratorCallback cb) { m_thumbnail_cb = cb; }
	void set_gcode_result(GCodeProcessorResult* result) { m_gcode_result = result; }

	bool switch_print_preprocess();
	bool can_switch_print();
	void set_current_plate(GUI::PartPlate* plate) { m_current_plate = plate; }
	GUI::PartPlate* get_current_plate() { return m_current_plate; }
	GCodeProcessorResult* get_current_gcode_result() { return m_gcode_result;}

	void set_slicing_completed_event(int event_id) { m_event_slicing_completed_id = event_id; }
	void set_finished_event(int event_id) { m_event_finished_id = event_id; }
	void set_export_began_event(int event_id) { m_event_export_began_id = event_id; }

	void set_export_finished_event(int event_id) { m_event_export_finished_id = event_id; }

	bool select_technology(PrinterTechnology tech);

	PrinterTechnology   current_printer_technology() const;
	const PrintBase*    current_print() const { return m_print; }
	const Print* 		fff_print() const { return m_fff_print; }
	Print* 				fff_print() { return m_fff_print; }
	const SLAPrint* 	sla_print() const { return m_sla_print; }
	std::string 		output_filepath_for_project(const boost::filesystem::path &project_path);

	bool start();
	bool stop();
	bool reset();

    PrintBase::ApplyStatus apply(const Model &model, const DynamicPrintConfig &config);
	void 		set_task(const PrintBase::TaskParams &params);
	bool 		empty() const;
    StringObjectException validate(std::vector<StringObjectException> *warnings = nullptr, Polygons* collison_polygons = nullptr, std::vector<std::pair<Polygon, float>>* height_polygons = nullptr);

	void schedule_export(const std::string &path, bool export_path_on_removable_media);
	void schedule_upload(Slic3r::PrintHostJob upload_job);
	void reset_export();
	bool is_export_scheduled() const { return ! m_export_path.empty(); }
	bool is_upload_scheduled() const { return ! m_upload_job.empty(); }

	enum State {
		STATE_INITIAL = 0,
		STATE_IDLE,
		STATE_STARTED,
		STATE_RUNNING,
		STATE_FINISHED,
		STATE_CANCELED,
		STATE_EXIT,
		STATE_EXITED,
	};
	State 	state() 	const { return m_state; }
	bool    idle() 		const { return m_state == STATE_IDLE; }
	bool    running() 	const { return m_state == STATE_STARTED || m_state == STATE_RUNNING || m_state == STATE_FINISHED || m_state == STATE_CANCELED; }
    bool    finished() const { return m_print->finished() && !m_gcode_result->moves.empty(); }
    bool    is_internal_cancelled() { return m_internal_cancelled; }

    friend class GUI::Plater;

private:
	void 	thread_proc();
	void 	thread_proc_safe() throw();
#ifdef _WIN32
	unsigned long 	thread_proc_safe_seh() throw();
	void 			thread_proc_safe_seh_throw() throw();
#endif
	void 	join_background_thread();
	void	stop_internal();

	void	process_fff();

    void	process_sla();

	void    call_process(std::exception_ptr &ex) throw();

#ifdef _WIN32
	unsigned long call_process_seh(std::exception_ptr &ex) throw();
	void    	  call_process_seh_throw(std::exception_ptr &ex) throw();
#endif
	PrintBase				   *m_print 			 = nullptr;
	Print 					   *m_fff_print 		 = nullptr;
	SLAPrint 				   *m_sla_print			 = nullptr;
	GCodeProcessorResult     *m_gcode_result 		 = nullptr;
	ThumbnailsGeneratorCallback m_thumbnail_cb 	     = nullptr;
	SL1Archive                  m_sla_archive;
	std::string 				m_temp_output_path;
	std::string 				m_export_path;
	bool 						m_export_path_on_removable_media = false;
	PrintHostJob                m_upload_job;
	boost::thread		 		m_thread;
	std::mutex 		 			m_mutex;
	std::condition_variable		m_condition;
	State 						m_state = STATE_INITIAL;

    struct UITask {
        enum State {
            Planned,
            Finished,
            Canceled,
        };
        State  					state = Planned;
        std::mutex 				mutex;
    	std::condition_variable	condition;
    };
    std::shared_ptr<UITask> 	m_ui_task;

	GUI::PartPlate* m_current_plate;
	PrinterTechnology m_printer_tech = ptUnknown;
	bool m_internal_cancelled = false;

    PrintState<BackgroundSlicingProcessStep, bspsCount>   	m_step_state;
	bool                set_step_started(BackgroundSlicingProcessStep step);
	void                set_step_done(BackgroundSlicingProcessStep step);
	bool 				is_step_done(BackgroundSlicingProcessStep step) const;
	bool                invalidate_step(BackgroundSlicingProcessStep step);
    bool                invalidate_all_steps();
    void                throw_if_canceled() const { if (m_print->canceled()) throw CanceledException(); }
	void				finalize_gcode();
	void				export_gcode();
    void                prepare_upload();
	ThumbnailsList		render_thumbnails(const ThumbnailsParams &params);
	bool 				execute_ui_task(std::function<void()> task);
	static void			cancel_ui_task(std::shared_ptr<BackgroundSlicingProcess::UITask> task);

	int 						m_event_slicing_completed_id 	= 0;
	int 						m_event_finished_id  			= 0;
	int                         m_event_export_began_id         = 0;
	int							m_event_export_finished_id		= 0;

};

};

#endif
