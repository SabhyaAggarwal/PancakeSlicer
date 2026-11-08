#ifndef slic3r_InstanceCheck_hpp_
#define slic3r_InstanceCheck_hpp_

#include "Event.hpp"
#include <vector>
#include <boost/filesystem/path.hpp>
#include <cassert>
#include <wx/event.h>

#if _WIN32
#include <windows.h>
#endif

#include <string>

#include <boost/filesystem.hpp>
#include <cstddef>

#if __linux__
#include <boost/thread.hpp>
#include <mutex>
#include <condition_variable>
#endif


namespace Slic3r {
bool    instance_check(int argc, char** argv, bool app_config_single_instance);

#if __APPLE__
void    send_message_mac(const std::string& msg, const std::string& version);
void    send_message_mac_closing(const std::string& msg, const std::string& version);


bool unlock_lockfile(const std::string& name, const std::string& path);
#endif

namespace GUI {

class MainFrame;

#if __linux__
    #define BACKGROUND_MESSAGE_LISTENER
#endif

using LoadFromOtherInstanceEvent = Event<std::vector<boost::filesystem::path>>;
using StartDownloadOtherInstanceEvent = Event<std::vector<std::string>>;
wxDECLARE_EVENT(EVT_LOAD_MODEL_OTHER_INSTANCE, LoadFromOtherInstanceEvent);
wxDECLARE_EVENT(EVT_START_DOWNLOAD_OTHER_INSTANCE, StartDownloadOtherInstanceEvent);

using InstanceGoToFrontEvent = SimpleEvent;
wxDECLARE_EVENT(EVT_INSTANCE_GO_TO_FRONT, InstanceGoToFrontEvent);

class OtherInstanceMessageHandler
{
public:
	OtherInstanceMessageHandler() = default;
	OtherInstanceMessageHandler(OtherInstanceMessageHandler const&) = delete;
	void operator=(OtherInstanceMessageHandler const&) = delete;
	~OtherInstanceMessageHandler() { assert(!m_initialized); }

	void    init(wxEvtHandler* callback_evt_handler);
	void    shutdown(MainFrame* main_frame);

	void           handle_message(const std::string& message);
#ifdef __APPLE__
	void           handle_message_other_closed();
#endif
#ifdef _WIN32
	static void    init_windows_properties(MainFrame* main_frame, size_t instance_hash);
#endif
private:
	bool                    m_initialized { false };
	wxEvtHandler*           m_callback_evt_handler { nullptr };

#ifdef BACKGROUND_MESSAGE_LISTENER
	boost::thread 			m_thread;
	std::condition_variable m_thread_stop_condition;
	mutable std::mutex 		m_thread_stop_mutex;
	bool 					m_stop{ false };
	
	void    listen();
#endif

#if __APPLE__
	void    register_for_messages(const std::string &version_hash);
	void    unregister_for_messages();
	void* m_impl_osx;
public: 
	void    bring_instance_forward();
#endif

};
}
}
#endif
