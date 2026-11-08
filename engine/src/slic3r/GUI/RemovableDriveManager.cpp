#include "RemovableDriveManager.hpp"
#include "libslic3r/Platform.hpp"
#include <boost/filesystem/operations.hpp>
#include <cstddef>
#include <boost/process/pipe.hpp>
#include <boost/process/search_path.hpp>
#include <boost/process/io.hpp>
#include <cassert>
#include <algorithm>
#include <boost/bind/bind.hpp>
#include <chrono>
#include <libslic3r/libslic3r.h>

#include <boost/nowide/convert.hpp>
#include <boost/log/trivial.hpp>
#include <string>
#include <vector>
#include <wx/utils.h>
#include <mutex>
#include <system_error>
#include <wx/event.h>
#include <utility>
#include <cwchar>

#if _WIN32
#include <windows.h>
#include <tchar.h>
#include <winioctl.h>
#include <shlwapi.h>

#include <Dbt.h>

#else
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <glob.h>
#include <pwd.h>
#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>
#include <boost/process.hpp>
#endif

namespace Slic3r {
namespace GUI { 

wxDEFINE_EVENT(EVT_REMOVABLE_DRIVE_EJECTED, RemovableDriveEjectEvent);
wxDEFINE_EVENT(EVT_REMOVABLE_DRIVES_CHANGED, RemovableDrivesChangedEvent);

#if _WIN32
std::vector<DriveData> RemovableDriveManager::search_for_removable_drives() const
{
	DWORD drives_mask = ::GetLogicalDrives();

	std::wstring volume_name;
	std::wstring file_system_name;
	std::vector<DriveData> current_drives;
	drives_mask >>= 2;
	for (char drive = 'C'; drive <= 'Z'; ++ drive, drives_mask >>= 1)
		if (drives_mask & 1) {
			std::string path { drive, ':' };
			UINT drive_type = ::GetDriveTypeA(path.c_str());
			if (drive_type ==  DRIVE_REMOVABLE) {
				std::wstring wpath = boost::nowide::widen(path);
				volume_name.resize(MAX_PATH + 1);
				file_system_name.resize(MAX_PATH + 1);
				BOOL error = ::GetVolumeInformationW(wpath.c_str(), volume_name.data(), sizeof(volume_name), nullptr, nullptr, nullptr, file_system_name.data(), sizeof(file_system_name));
				if (error != 0) {
					volume_name.erase(volume_name.begin() + wcslen(volume_name.c_str()), volume_name.end());
					if (! file_system_name.empty()) {
						ULARGE_INTEGER free_space;
						::GetDiskFreeSpaceExW(wpath.c_str(), &free_space, nullptr, nullptr);
						if (free_space.QuadPart > 0) {
							path += "\\";
							current_drives.emplace_back(DriveData{ boost::nowide::narrow(volume_name), path });
						}
					}
				}
			}
		}
	return current_drives;
}

void RemovableDriveManager::eject_drive()
{
	if (m_last_save_path.empty())
		return;

#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	this->update();
#endif
	BOOST_LOG_TRIVIAL(info) << "Ejecting started"; 
	std::scoped_lock<std::mutex> lock(m_drives_mutex);
	auto it_drive_data = this->find_last_save_path_drive_data();
	if (it_drive_data != m_current_drives.end()) {
		std::string mpath = "\\\\.\\" + m_last_save_path;
		mpath = mpath.substr(0, mpath.size() - 1);
		HANDLE handle = CreateFileW(boost::nowide::widen(mpath).c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			BOOST_LOG_TRIVIAL(error) << "Ejecting " << mpath << " failed (handle == INVALID_HANDLE_VALUE): " << GetLastError();
			assert(m_callback_evt_handler);
			if (m_callback_evt_handler)
				wxPostEvent(m_callback_evt_handler, RemovableDriveEjectEvent(EVT_REMOVABLE_DRIVE_EJECTED, std::pair<DriveData, bool>(*it_drive_data, false)));
			return;
		}
		DWORD deviceControlRetVal(0);
		BOOL e1 = DeviceIoControl(handle, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &deviceControlRetVal, nullptr);
		BOOST_LOG_TRIVIAL(debug) << "FSCTL_LOCK_VOLUME " << e1 << " ; " << deviceControlRetVal << " ; " << GetLastError();
		BOOL e2 = DeviceIoControl(handle, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &deviceControlRetVal, nullptr);
		BOOST_LOG_TRIVIAL(debug) << "FSCTL_DISMOUNT_VOLUME " << e2 << " ; " << deviceControlRetVal << " ; " << GetLastError();
		BOOL error = DeviceIoControl(handle, IOCTL_STORAGE_EJECT_MEDIA, nullptr, 0, nullptr, 0, &deviceControlRetVal, nullptr);
		if (error == 0) {
			CloseHandle(handle);
			BOOST_LOG_TRIVIAL(error) << "Ejecting " << mpath << " failed (IOCTL_STORAGE_EJECT_MEDIA)" << deviceControlRetVal << " " << GetLastError();
			assert(m_callback_evt_handler);
			if (m_callback_evt_handler)
				wxPostEvent(m_callback_evt_handler, RemovableDriveEjectEvent(EVT_REMOVABLE_DRIVE_EJECTED, std::pair<DriveData, bool>(*it_drive_data, false)));
			return;
		}
		CloseHandle(handle);
		BOOST_LOG_TRIVIAL(info) << "Ejecting finished";
		assert(m_callback_evt_handler);
		if (m_callback_evt_handler) 
			wxPostEvent(m_callback_evt_handler, RemovableDriveEjectEvent(EVT_REMOVABLE_DRIVE_EJECTED, std::pair< DriveData, bool >(std::move(*it_drive_data), true)));
		m_current_drives.erase(it_drive_data);
	}
}

std::string RemovableDriveManager::get_removable_drive_path(const std::string &path)
{
#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	this->update();
#endif

	std::scoped_lock<std::mutex> lock(m_drives_mutex);
	if (m_current_drives.empty())
		return std::string();
	std::size_t found = path.find_last_of("\\");
	std::string new_path = path.substr(0, found);
	int letter = PathGetDriveNumberW(boost::nowide::widen(new_path).c_str());
	for (const DriveData &drive_data : m_current_drives) {
		char drive = drive_data.path[0];
		if (drive == 'A' + letter)
			return path;	
	}
	return m_current_drives.front().path;
}

std::string RemovableDriveManager::get_removable_drive_from_path(const std::string& path)
{
	std::scoped_lock<std::mutex> lock(m_drives_mutex);
	std::size_t found = path.find_last_of("\\");
	std::string new_path = path.substr(0, found);
	int letter = PathGetDriveNumberW(boost::nowide::widen(new_path).c_str());	
	for (const DriveData &drive_data : m_current_drives) {
		assert(! drive_data.path.empty());
		if (drive_data.path.front() == 'A' + letter)
			return drive_data.path;
	}
	return std::string();
}

void RemovableDriveManager::volumes_changed()
{
	if (m_initialized) {
	    m_wakeup = true;
		m_thread_stop_condition.notify_all();
	}
}

#else

namespace search_for_drives_internal 
{
	static bool compare_filesystem_id(const std::string &path_a, const std::string &path_b)
	{
		struct stat buf;
		stat(path_a.c_str() ,&buf);
		dev_t id_a = buf.st_dev;
		stat(path_b.c_str() ,&buf);
		dev_t id_b = buf.st_dev;
		return id_a == id_b;
	}

	void inspect_file(const std::string &path, const std::string &parent_path, std::vector<DriveData> &out)
	{

		if (
#ifdef __linux__
			platform_flavor() == PlatformFlavor::LinuxOnChromium ||
#endif
			! compare_filesystem_id(path, parent_path)) {
			boost::system::error_code ec;
			boost::filesystem::space_info si = boost::filesystem::space(path, ec);
			if (!ec && si.available != 0) {
				struct stat buf;
				stat(path.c_str(), &buf);
				uid_t uid = buf.st_uid;
				if (getuid() == uid)
					out.emplace_back(DriveData{ boost::filesystem::path(path).stem().string(), path });
			}
		}
	}

#if ! __APPLE__
	static void search_path(const std::string &path, const std::string &parent_path, std::vector<DriveData> &out)
	{
	    glob_t globbuf;
		globbuf.gl_offs = 2;
		int error = glob(path.c_str(), GLOB_TILDE, NULL, &globbuf);
		if (error == 0) {
			for (size_t i = 0; i < globbuf.gl_pathc; ++ i)
				inspect_file(globbuf.gl_pathv[i], parent_path, out);
		} else {
		}
		globfree(&globbuf);
	}
#endif
}

std::vector<DriveData> RemovableDriveManager::search_for_removable_drives() const
{
	std::vector<DriveData> current_drives;

#if __APPLE__

	this->list_devices(current_drives);

#else

   	if (platform_flavor() == PlatformFlavor::LinuxOnChromium) {
		search_for_drives_internal::search_path("/mnt/chromeos/removable/*", "/mnt/chromeos/removable", current_drives);
   	} else {
		search_for_drives_internal::search_path("/media/*", "/media", current_drives);

		std::string path = wxGetUserId().ToUTF8().data();
		std::string pp(path);

		pp = "/media/"+pp;
		path = "/media/" + path + "/*";
		search_for_drives_internal::search_path(path, pp, current_drives);

		path = "/run" + path;
		pp = "/run"+pp;
		search_for_drives_internal::search_path(path, pp, current_drives);
	}

#endif

	return current_drives;
}

void RemovableDriveManager::eject_drive()
{
	if (m_last_save_path.empty())
		return;

#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	this->update();
#endif
#if __APPLE__
	this->eject_thread_finish();
#endif

	BOOST_LOG_TRIVIAL(info) << "Ejecting started";

	DriveData drive_data;
	{
		std::scoped_lock<std::mutex> lock(m_drives_mutex);
		auto it_drive_data = this->find_last_save_path_drive_data();
		if (it_drive_data == m_current_drives.end())
			return;
		drive_data = *it_drive_data;
	}

	std::string correct_path(m_last_save_path);
#if __APPLE__
	m_eject_thread = new boost::thread([this, correct_path, drive_data]()
#endif
	{
		
		BOOST_LOG_TRIVIAL(info) << "Ejecting started";
		boost::process::ipstream istd_err;
    	boost::process::child child(
#if __APPLE__		
			boost::process::search_path("diskutil"), "eject", correct_path.c_str(), (boost::process::std_out & boost::process::std_err) > istd_err);
#else
    		boost::process::search_path("umount"), correct_path.c_str(), (boost::process::std_out & boost::process::std_err) > istd_err);
#endif
		std::string line;
		while (child.running() && std::getline(istd_err, line)) {
			BOOST_LOG_TRIVIAL(trace) << line;
		}
		std::error_code ec;
		child.wait(ec);
		bool success = false;
		if (ec) {
			BOOST_LOG_TRIVIAL(error) << "boost::process::child::wait() failed during Ejection. State of Ejection is unknown. Error code: " << ec.value();
		} else {
			int err = child.exit_code();
	    	if (err) {
	    		BOOST_LOG_TRIVIAL(error) << "Ejecting failed. Exit code: " << err;
	    	} else {
				BOOST_LOG_TRIVIAL(info) << "Ejecting finished";
				success = true;
			}
		}
		assert(m_callback_evt_handler);
		if (m_callback_evt_handler) 
			wxPostEvent(m_callback_evt_handler, RemovableDriveEjectEvent(EVT_REMOVABLE_DRIVE_EJECTED, std::pair<DriveData, bool>(drive_data, success)));
		if (success) {
			std::scoped_lock<std::mutex> lock(m_drives_mutex);
			auto it = std::find(m_current_drives.begin(), m_current_drives.end(), drive_data);
			if (it != m_current_drives.end())
				m_current_drives.erase(it);
		}
	}
#if __APPLE__
	);
#endif
}

std::string RemovableDriveManager::get_removable_drive_path(const std::string &path)
{
#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	this->update();
#endif

	std::size_t found = path.find_last_of("/");
	std::string new_path = found == path.size() - 1 ? path.substr(0, found) : path;

	std::scoped_lock<std::mutex> lock(m_drives_mutex);
	for (const DriveData &data : m_current_drives)
		if (search_for_drives_internal::compare_filesystem_id(new_path, data.path))
			return path;
	return m_current_drives.empty() ? std::string() : m_current_drives.front().path;
}

std::string RemovableDriveManager::get_removable_drive_from_path(const std::string& path)
{
	std::string new_path(path);
	if (!boost::filesystem::is_directory(path)) {
		std::size_t found = path.find_last_of("/");
		if (found != std::string::npos)
			new_path.erase(found);
	}

	std::scoped_lock<std::mutex> lock(m_drives_mutex);
	for (const DriveData &drive_data : m_current_drives)
		if (search_for_drives_internal::compare_filesystem_id(new_path, drive_data.path))
			return drive_data.path;
	return std::string();
}
#endif

void RemovableDriveManager::init(wxEvtHandler *callback_evt_handler)
{
	assert(! m_initialized);
	assert(m_callback_evt_handler == nullptr);

	if (m_initialized)
		return;

	m_initialized = true;
	m_callback_evt_handler = callback_evt_handler;

#if __APPLE__
    this->register_window_osx();
#endif

#ifdef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	this->update();
#else
	m_thread = boost::thread((boost::bind(&RemovableDriveManager::thread_proc, this)));
#endif
}

void RemovableDriveManager::shutdown()
{
#if __APPLE__
	this->eject_thread_finish();
#endif

#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
    if (m_thread.joinable()) {
		{
			std::lock_guard<std::mutex> lck(m_thread_stop_mutex);
			m_stop = true;
		}
		m_thread_stop_condition.notify_all();
		m_thread.join();
		m_stop = false;
	}
#endif

	m_initialized = false;
	m_callback_evt_handler = nullptr;
}

bool RemovableDriveManager::set_and_verify_last_save_path(const std::string &path)
{
#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
	this->update();
#endif

#ifdef __APPLE__
	m_last_save_path = path;
#else
	m_last_save_path = this->get_removable_drive_from_path(path);
#endif

	
	m_exporting_finished = false;
	return ! m_last_save_path.empty();
}

RemovableDriveManager::RemovableDrivesStatus RemovableDriveManager::status()
{

	RemovableDriveManager::RemovableDrivesStatus out;
	{
		std::scoped_lock<std::mutex> lock(m_drives_mutex);
		out.has_eject = 
			platform_flavor() != PlatformFlavor::LinuxOnChromium &&
			this->find_last_save_path_drive_data() != m_current_drives.end();
		out.has_removable_drives = ! m_current_drives.empty();
	}
	if (! out.has_eject) 
		m_last_save_path.clear();
	out.has_eject = out.has_eject && m_exporting_finished;
	return out;
}

void RemovableDriveManager::update()
{
	std::unique_lock<std::mutex> inside_update_lock(m_inside_update_mutex, std::defer_lock);
#ifdef _WIN32
	m_wakeup = false;
#endif
	if (inside_update_lock.try_lock()) {
		std::vector<DriveData> current_drives = this->search_for_removable_drives();
		std::scoped_lock<std::mutex> lock(m_drives_mutex);
		std::sort(current_drives.begin(), current_drives.end());
		if (current_drives != m_current_drives) {
			assert(m_callback_evt_handler);
			if (m_callback_evt_handler)
				wxPostEvent(m_callback_evt_handler, RemovableDrivesChangedEvent(EVT_REMOVABLE_DRIVES_CHANGED));
		}
		m_current_drives = std::move(current_drives);
	} else {
		inside_update_lock.lock();
	}
}

#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
void RemovableDriveManager::thread_proc()
{
#ifdef _WIN32
    m_wakeup = true;
#endif

	for (;;) {
		{
			std::unique_lock<std::mutex> lck(m_thread_stop_mutex);
#ifdef _WIN32
			m_thread_stop_condition.wait(lck, [this]{ return m_stop || m_wakeup; });
#else
			m_thread_stop_condition.wait_for(lck, std::chrono::seconds(2), [this]{ return m_stop; });
#endif
		}
		if (m_stop)
			break;
		this->update();
	}
}
#endif

std::vector<DriveData>::const_iterator RemovableDriveManager::find_last_save_path_drive_data() const
{
	return Slic3r::binary_find_by_predicate(m_current_drives.begin(), m_current_drives.end(),
		[this](const DriveData &data){ return data.path < m_last_save_path; }, 
		[this](const DriveData &data){ return data.path == m_last_save_path; });
}

#if __APPLE__
void RemovableDriveManager::eject_thread_finish()
{
	if (m_eject_thread) {
		m_eject_thread->join();
		delete m_eject_thread;
		m_eject_thread = nullptr;
	}
}
#endif

}}
