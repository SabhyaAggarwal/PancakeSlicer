#ifndef slic3r_GUI_RemovableDriveManager_hpp_
#define slic3r_GUI_RemovableDriveManager_hpp_

#include <utility>
#include <vector>
#include <string>

#include <boost/thread.hpp>
#include <mutex>
#include <condition_variable>
#include <wx/event.h>

#include "Event.hpp"
#include <atomic>

namespace Slic3r {
namespace GUI {

struct DriveData
{
	std::string name;
	std::string path;

	void clear() {
		name.clear();
		path.clear();
	}
	bool empty() const {
		return path.empty();
	}
};

inline bool operator< (const DriveData &lhs, const DriveData &rhs) { return lhs.path < rhs.path; }
inline bool operator> (const DriveData &lhs, const DriveData &rhs) { return lhs.path > rhs.path; }
inline bool operator==(const DriveData &lhs, const DriveData &rhs) { return lhs.path == rhs.path; }

using RemovableDriveEjectEvent = Event<std::pair<DriveData, bool>>;
wxDECLARE_EVENT(EVT_REMOVABLE_DRIVE_EJECTED, RemovableDriveEjectEvent);

using RemovableDrivesChangedEvent = SimpleEvent;
wxDECLARE_EVENT(EVT_REMOVABLE_DRIVES_CHANGED, RemovableDrivesChangedEvent);

#if __APPLE__
	#define REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
#endif

class RemovableDriveManager
{
public:
	RemovableDriveManager() = default;
	RemovableDriveManager(RemovableDriveManager const&) = delete;
	void operator=(RemovableDriveManager const&) = delete;
	~RemovableDriveManager() {  }

	void 		init(wxEvtHandler *callback_evt_handler);
	void 		shutdown();

	std::string get_removable_drive_path(const std::string &path);
	bool        is_path_on_removable_drive(const std::string &path) { return this->get_removable_drive_path(path) == path; }

	bool 		set_and_verify_last_save_path(const std::string &path);
	void 		eject_drive();

	struct RemovableDrivesStatus {
		bool 	has_removable_drives { false };
		bool 	has_eject { false };
	};
	RemovableDrivesStatus status();

	void 		update();
	void        set_exporting_finished(bool b) { m_exporting_finished = b; }
#ifdef _WIN32
	void 		volumes_changed();
#endif

private:
	bool 			 		m_initialized { false };
	wxEvtHandler*			m_callback_evt_handler { nullptr };

#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	void 					thread_proc();
	boost::thread 			m_thread;
	std::condition_variable m_thread_stop_condition;
	mutable std::mutex 		m_thread_stop_mutex;
	bool 					m_stop { false };
#ifdef _WIN32
    std::atomic<bool>		m_wakeup { false };
#endif
#endif
	std::vector<DriveData> 	search_for_removable_drives() const;

	std::vector<DriveData> 	m_current_drives;
	mutable std::mutex 		m_drives_mutex;
	mutable std::mutex 		m_inside_update_mutex;

	std::string 			get_removable_drive_from_path(const std::string& path);
	std::vector<DriveData>::const_iterator find_last_save_path_drive_data() const;
	std::string 			m_last_save_path;
	bool                    m_exporting_finished;
#if __APPLE__
    void register_window_osx();
    void unregister_window_osx();
    void list_devices(std::vector<DriveData> &out) const;
    void eject_device(const std::string &path);
    void *m_impl_osx;
    boost::thread *m_eject_thread { nullptr };
    void eject_thread_finish();
#endif
};

}}

#endif
