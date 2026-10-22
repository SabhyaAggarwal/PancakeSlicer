#include "Utils.hpp"
#include "HeapTrim.hpp"
#include "Exception.hpp"
#include "I18N.hpp"

#include <atomic>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/keywords/severity.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/exception.hpp>
#include <cstdio>
#include <boost/log/keywords/file_name.hpp>
#include <boost/log/keywords/rotation_size.hpp>
#include <boost/log/keywords/format.hpp>
#include <boost/log/expressions/formatters/stream.hpp>
#include <boost/log/expressions/attr.hpp>
#include <boost/log/expressions/formatters/date_time.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/log/attributes/current_thread_id.hpp>
#include <boost/log/expressions/message.hpp>
#include <boost/log/keywords/auto_flush.hpp>
#include <initializer_list>
#include <boost/filesystem/file_status.hpp>
#include <cstdint>
#include <boost/filesystem/directory.hpp>
#include <fstream>
#include <iosfwd>
#include <cstring>
#include <cassert>
#include <boost/locale/conversion.hpp>
#include <boost/uuid/detail/md5.hpp>
#include <iterator>
#include <functional>
#include <exception>
#include <locale>
#include <ctime>
#include <cstdarg>
#include <iostream>
#include <map>
#include <set>
#include <stdio.h>
#include <filesystem>
#include <sstream>
#include <cerrno>
#include <mutex>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <string>
#include <system_error>
#include <string_view>
#include <vector>

#include "Semver.hpp"
#include "format.hpp"
#include "Platform.hpp"
#include "libslic3r.h"
#include "Preset.hpp"
#include "PresetCacheFormat.hpp"
#include "libslic3r_version.h"

#ifdef __APPLE__
#include "MacUtils.hpp"
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif

#ifdef WIN32
	#include <windows.h>
	#include <psapi.h>
	#include <direct.h>
	#include <io.h>
#else
	#include <unistd.h>
	#include <sys/types.h>
	#include <sys/param.h>
    #include <sys/resource.h>
	#ifdef BSD
		#include <sys/sysctl.h>
	#endif
	#ifdef __APPLE__
		#include <mach/mach.h>
		#include <libproc.h>
	#endif
	#ifdef __linux__
		#include <sys/stat.h>
		#include <fcntl.h>
		#include <sys/sendfile.h>
		#include <dirent.h>
		#include <stdio.h>
	#endif
#endif

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/support/date_time.hpp>

#include <tbb/scalable_allocator.h>

#include <boost/core/null_deleter.hpp>
#include <boost/locale.hpp>
#include <boost/make_shared.hpp>
#include <boost/shared_ptr.hpp>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/nowide/cstdio.hpp>

#include <tbb/tbb.h>
#include <string.h>

namespace boost::posix_time { class ptime; }
#if ! defined(TBB_VERSION_MAJOR)
    #include <tbb/version.h>
#endif
#if ! defined(TBB_VERSION_MAJOR)
    static_assert(false, "TBB_VERSION_MAJOR not defined");
#endif
#if TBB_VERSION_MAJOR >= 2021
    #define TBB_HAS_GLOBAL_CONTROL
#endif
#ifdef TBB_HAS_GLOBAL_CONTROL
    #include <tbb/global_control.h>
#else
    #include <tbb/task_scheduler_init.h>
#endif

#if defined(__linux__) || defined(__GNUC__ )
#include <strings.h>
#endif

#ifdef _MSC_VER
    #define strcasecmp _stricmp
#endif

namespace Slic3r {

static boost::log::trivial::severity_level logSeverity = boost::log::trivial::error;

static boost::log::trivial::severity_level level_to_boost(unsigned level)
{
    switch (level) {
    case 0: return boost::log::trivial::fatal;
    case 1: return boost::log::trivial::error;
    case 2: return boost::log::trivial::warning;
    case 3: return boost::log::trivial::info;
    case 4: return boost::log::trivial::debug;
    default: return boost::log::trivial::trace;
    }
}

void set_logging_level(unsigned int level)
{
    logSeverity = level_to_boost(level);

    const std::string version = SoftFever_VERSION;
    if (level < (unsigned int) boost::log::trivial::info &&
        (boost::algorithm::icontains(version, "dev") || boost::algorithm::icontains(version, "alpha") ||
         boost::algorithm::icontains(version, "beta"))) {
        logSeverity = boost::log::trivial::info;
    }

    boost::log::core::get()->set_filter
    (
        boost::log::trivial::severity >= logSeverity
    );
}

void set_logging_file(const std::string &file)
{
	boost::log::add_file_log(file);
}

unsigned int level_string_to_boost(std::string level)
{
    std::map<std::string, int> Control_Param;
    Control_Param["fatal"] = 0;
    Control_Param["error"] = 1;
    Control_Param["warning"] = 2;
    Control_Param["info"] = 3;
    Control_Param["debug"] = 4;
    Control_Param["trace"] = 5;

    return Control_Param[level];
}

std::string get_string_logging_level(unsigned level)
{
    switch (level) {
    case 0: return "fatal";
    case 1: return "error";
    case 2: return "warning";
    case 3: return "info";
    case 4: return "debug";
    case 5: return "trace";
    default: return "error";
    }
}

unsigned get_logging_level()
{
    switch (logSeverity) {
    case boost::log::trivial::fatal : return 0;
    case boost::log::trivial::error : return 1;
    case boost::log::trivial::warning : return 2;
    case boost::log::trivial::info : return 3;
    case boost::log::trivial::debug : return 4;
    case boost::log::trivial::trace : return 5;
    default: return 1;
    }
}

boost::shared_ptr<boost::log::sinks::synchronous_sink<boost::log::sinks::text_file_backend>> g_log_sink;
boost::shared_ptr<boost::log::sinks::asynchronous_sink<boost::log::sinks::text_ostream_backend>> g_console_log_sink;

static struct RunOnInit {
    RunOnInit() {
        set_logging_level(2);

    }
} g_RunOnInit;

void trace(unsigned int level, const char *message)
{
    boost::log::trivial::severity_level severity = level_to_boost(level);

    BOOST_LOG_STREAM_WITH_PARAMS(::boost::log::trivial::logger::get(),\
        (::boost::log::keywords::severity = severity)) << message;
}

void disable_multi_threading()
{
#ifdef TBB_HAS_GLOBAL_CONTROL
    tbb::global_control(tbb::global_control::max_allowed_parallelism, 1);
#else
    static tbb::task_scheduler_init *tbb_init = new tbb::task_scheduler_init(1);
    UNUSED(tbb_init);
#endif
}

static std::string g_var_dir;

void set_var_dir(const std::string &dir)
{
    g_var_dir = dir;
}

const std::string& var_dir()
{
    return g_var_dir;
}

std::string var(const std::string &file_name)
{
    boost::system::error_code ec;
    if (boost::filesystem::exists(file_name, ec)) {
       return file_name;
    }

    auto file = (boost::filesystem::path(g_var_dir) / file_name).make_preferred();
    return file.string();
}

static std::string g_resources_dir;

void set_resources_dir(const std::string &dir)
{
    g_resources_dir = dir;
}

const std::string& resources_dir()
{
    return g_resources_dir;
}

static std::string g_temporary_dir;
void set_temporary_dir(const std::string &dir)
{
    g_temporary_dir = dir;
}

const std::string& temporary_dir()
{
    return g_temporary_dir;
}

static std::string g_local_dir;

void set_local_dir(const std::string &dir)
{
    g_local_dir = dir;
}

const std::string& localization_dir()
{
	return g_local_dir;
}

static std::string g_sys_shapes_dir;

void set_sys_shapes_dir(const std::string &dir)
{
    g_sys_shapes_dir = dir;
}

const std::string& sys_shapes_dir()
{
	return g_sys_shapes_dir;
}

static std::string g_custom_gcodes_dir;

void set_custom_gcodes_dir(const std::string &dir)
{
    g_custom_gcodes_dir = dir;
}

const std::string& custom_gcodes_dir()
{
    return g_custom_gcodes_dir;
}

Slic3r::I18N::translate_fn_type Slic3r::I18N::translate_fn = nullptr;
static std::string g_data_dir;

void set_data_dir(const std::string &dir)
{
    g_data_dir = dir;
    if (!g_data_dir.empty() && !boost::filesystem::exists(g_data_dir)) {
        try {
            boost::filesystem::create_directories(g_data_dir);
        } catch (const boost::filesystem::filesystem_error &ex) {
            BOOST_LOG_TRIVIAL(error) << "set_data_dir: failed to create data directory " << g_data_dir << ": " << ex.what();
        }
    }
}

const std::string& data_dir()
{
    return g_data_dir;
}

std::string custom_shapes_dir()
{
    return (boost::filesystem::path(g_data_dir) / "shapes").string();
}

std::string handy_models_dir()
{
    return (boost::filesystem::path(resources_dir()) / "handy_models").string();
}

static std::atomic<bool> debug_out_path_called(false);

std::string debug_out_path(const char *name, ...)
{
	auto svg_folder = boost::filesystem::path(g_data_dir) / "SVG/";
    if (! debug_out_path_called.exchange(true)) {
		if (!boost::filesystem::exists(svg_folder)) {
			boost::filesystem::create_directory(svg_folder);
		}
		std::string path = boost::filesystem::system_complete(svg_folder).string();
        printf("Debugging output files will be written to %s\n", path.c_str());
    }
	char buffer[2048];
	va_list args;
	va_start(args, name);
	std::vsprintf(buffer, name, args);
	va_end(args);

	std::string buf(buffer);
	if (size_t pos = buf.find_first_of('/'); pos != std::string::npos) {
		std::string sub_dir = buf.substr(0, pos);
		std::filesystem::create_directory(svg_folder.string() + sub_dir);
	}
	return svg_folder.string() + std::string(buffer);
}

namespace logging = boost::log;
namespace expr = boost::log::expressions;
namespace keywords = boost::log::keywords;
namespace attrs = boost::log::attributes;
namespace sinks = boost::log::sinks;

void shutdown_console_logging()
{
	if (!g_console_log_sink)
		return;

	auto console_sink = g_console_log_sink;
	boost::log::core::get()->remove_sink(console_sink);
	console_sink->stop();
	g_console_log_sink.reset();
}

void set_log_path_and_level(const std::string& file, unsigned int level)
{
#ifdef __APPLE__
	if (!is_macos_support_boost_add_file_log()) {
		return;
	}
#endif

	auto log_folder = boost::filesystem::path(g_data_dir) / "log";
	if (!boost::filesystem::exists(log_folder)) {
		boost::filesystem::create_directory(log_folder);
	}
	auto full_path = (log_folder / file).make_preferred();

	g_log_sink = boost::log::add_file_log(
		keywords::file_name = full_path.string() + ".%N",
		keywords::rotation_size = 100 * 1024 * 1024,
		keywords::format =
		(
			expr::stream
			<< "[" << expr::attr< logging::trivial::severity_level >("Severity") << "]\t"
			<< expr::format_date_time< boost::posix_time::ptime >("TimeStamp", "%Y-%m-%d %H:%M:%S.%f")
			<<"[Thread " << expr::attr<attrs::current_thread_id::value_type>("ThreadID") << "]"
			<< ":" << expr::smessage
		),
		keywords::auto_flush = true
	);

	shutdown_console_logging();

#ifdef SLIC3R_CONSOLE_LOG
	auto console_backend = boost::make_shared<sinks::text_ostream_backend>();
	console_backend->add_stream(boost::shared_ptr<std::ostream>(&std::cout, boost::null_deleter()));
	console_backend->auto_flush(true);

	g_console_log_sink = boost::make_shared<sinks::asynchronous_sink<sinks::text_ostream_backend>>(console_backend);
	g_console_log_sink->set_formatter(
		expr::stream
		<< "[" << expr::attr< logging::trivial::severity_level >("Severity") << "]\t"
		<< expr::format_date_time< boost::posix_time::ptime >("TimeStamp", "%Y-%m-%d %H:%M:%S.%f") << " "
		<<"[Thread " << expr::attr<attrs::current_thread_id::value_type>("ThreadID") << "]"
		<< ": " << expr::smessage
	);
	boost::log::core::get()->add_sink(g_console_log_sink);
#endif

	logging::add_common_attributes();

	set_logging_level(level);

	return;
}

void flush_logs()
{
	if (g_log_sink)
		g_log_sink->flush();

	return;
}

boost::filesystem::path get_log_file_name()
{
    if (g_log_sink)
        return g_log_sink->locked_backend()->get_current_file_name();
    return {};
}

#ifdef _WIN32
namespace WindowsSupport
{
	template <typename HandleTraits>
	class ScopedHandle {
		typedef typename HandleTraits::handle_type handle_type;
		handle_type Handle;
		ScopedHandle(const ScopedHandle &other) = delete;
		void operator=(const ScopedHandle &other) = delete;
	public:
		ScopedHandle() : Handle(HandleTraits::GetInvalid()) {}
	  	explicit ScopedHandle(handle_type h) : Handle(h) {}
	  	~ScopedHandle() { if (HandleTraits::IsValid(Handle)) HandleTraits::Close(Handle); }
	  	handle_type take() {
	    	handle_type t = Handle;
	    	Handle = HandleTraits::GetInvalid();
	    	return t;
	  	}
	  	ScopedHandle &operator=(handle_type h) {
	    	if (HandleTraits::IsValid(Handle))
	      		HandleTraits::Close(Handle);
	    	Handle = h;
	    	return *this;
	  	}
	  	explicit operator bool() const { return HandleTraits::IsValid(Handle) ? true : false; }
	  	operator handle_type() const { return Handle; }
	};

	struct CommonHandleTraits {
	  	typedef HANDLE handle_type;
	  	static handle_type GetInvalid() { return INVALID_HANDLE_VALUE; }
	  	static void Close(handle_type h) { ::CloseHandle(h); }
	  	static bool IsValid(handle_type h) { return h != GetInvalid(); }
	};

	typedef ScopedHandle<CommonHandleTraits> ScopedFileHandle;

	std::error_code map_windows_error(unsigned windows_error_code)
	{
		#define MAP_ERR_TO_COND(x, y) case x: return std::make_error_code(std::errc::y)
		switch (windows_error_code) {
			MAP_ERR_TO_COND(ERROR_ACCESS_DENIED, permission_denied);
			MAP_ERR_TO_COND(ERROR_ALREADY_EXISTS, file_exists);
			MAP_ERR_TO_COND(ERROR_BAD_UNIT, no_such_device);
			MAP_ERR_TO_COND(ERROR_BUFFER_OVERFLOW, filename_too_long);
			MAP_ERR_TO_COND(ERROR_BUSY, device_or_resource_busy);
			MAP_ERR_TO_COND(ERROR_BUSY_DRIVE, device_or_resource_busy);
			MAP_ERR_TO_COND(ERROR_CANNOT_MAKE, permission_denied);
			MAP_ERR_TO_COND(ERROR_CANTOPEN, io_error);
			MAP_ERR_TO_COND(ERROR_CANTREAD, io_error);
			MAP_ERR_TO_COND(ERROR_CANTWRITE, io_error);
			MAP_ERR_TO_COND(ERROR_CURRENT_DIRECTORY, permission_denied);
			MAP_ERR_TO_COND(ERROR_DEV_NOT_EXIST, no_such_device);
			MAP_ERR_TO_COND(ERROR_DEVICE_IN_USE, device_or_resource_busy);
			MAP_ERR_TO_COND(ERROR_DIR_NOT_EMPTY, directory_not_empty);
			MAP_ERR_TO_COND(ERROR_DIRECTORY, invalid_argument);
			MAP_ERR_TO_COND(ERROR_DISK_FULL, no_space_on_device);
			MAP_ERR_TO_COND(ERROR_FILE_EXISTS, file_exists);
			MAP_ERR_TO_COND(ERROR_FILE_NOT_FOUND, no_such_file_or_directory);
			MAP_ERR_TO_COND(ERROR_HANDLE_DISK_FULL, no_space_on_device);
			MAP_ERR_TO_COND(ERROR_INVALID_ACCESS, permission_denied);
			MAP_ERR_TO_COND(ERROR_INVALID_DRIVE, no_such_device);
			MAP_ERR_TO_COND(ERROR_INVALID_FUNCTION, function_not_supported);
			MAP_ERR_TO_COND(ERROR_INVALID_HANDLE, invalid_argument);
			MAP_ERR_TO_COND(ERROR_INVALID_NAME, invalid_argument);
			MAP_ERR_TO_COND(ERROR_LOCK_VIOLATION, no_lock_available);
			MAP_ERR_TO_COND(ERROR_LOCKED, no_lock_available);
			MAP_ERR_TO_COND(ERROR_NEGATIVE_SEEK, invalid_argument);
			MAP_ERR_TO_COND(ERROR_NOACCESS, permission_denied);
			MAP_ERR_TO_COND(ERROR_NOT_ENOUGH_MEMORY, not_enough_memory);
			MAP_ERR_TO_COND(ERROR_NOT_READY, resource_unavailable_try_again);
			MAP_ERR_TO_COND(ERROR_OPEN_FAILED, io_error);
			MAP_ERR_TO_COND(ERROR_OPEN_FILES, device_or_resource_busy);
			MAP_ERR_TO_COND(ERROR_OUTOFMEMORY, not_enough_memory);
			MAP_ERR_TO_COND(ERROR_PATH_NOT_FOUND, no_such_file_or_directory);
			MAP_ERR_TO_COND(ERROR_BAD_NETPATH, no_such_file_or_directory);
			MAP_ERR_TO_COND(ERROR_READ_FAULT, io_error);
			MAP_ERR_TO_COND(ERROR_RETRY, resource_unavailable_try_again);
			MAP_ERR_TO_COND(ERROR_SEEK, io_error);
			MAP_ERR_TO_COND(ERROR_SHARING_VIOLATION, permission_denied);
			MAP_ERR_TO_COND(ERROR_TOO_MANY_OPEN_FILES, too_many_files_open);
			MAP_ERR_TO_COND(ERROR_WRITE_FAULT, io_error);
			MAP_ERR_TO_COND(ERROR_WRITE_PROTECT, permission_denied);
			MAP_ERR_TO_COND(WSAEACCES, permission_denied);
			MAP_ERR_TO_COND(WSAEBADF, bad_file_descriptor);
			MAP_ERR_TO_COND(WSAEFAULT, bad_address);
			MAP_ERR_TO_COND(WSAEINTR, interrupted);
			MAP_ERR_TO_COND(WSAEINVAL, invalid_argument);
			MAP_ERR_TO_COND(WSAEMFILE, too_many_files_open);
			MAP_ERR_TO_COND(WSAENAMETOOLONG, filename_too_long);
		default:
			return std::error_code(windows_error_code, std::system_category());
		}
		#undef MAP_ERR_TO_COND
	}

	static std::error_code rename_internal(HANDLE from_handle, const std::wstring &wide_to, bool replace_if_exists)
	{
		std::vector<char> rename_info_buf(sizeof(FILE_RENAME_INFO) - sizeof(wchar_t) + (wide_to.size() * sizeof(wchar_t)));
		FILE_RENAME_INFO &rename_info = *reinterpret_cast<FILE_RENAME_INFO*>(rename_info_buf.data());
		rename_info.ReplaceIfExists = replace_if_exists;
		rename_info.RootDirectory = 0;
		rename_info.FileNameLength = DWORD(wide_to.size() * sizeof(wchar_t));
		std::copy(wide_to.begin(), wide_to.end(), &rename_info.FileName[0]);

		::SetLastError(ERROR_SUCCESS);
		if (! ::SetFileInformationByHandle(from_handle, FileRenameInfo, &rename_info, (DWORD)rename_info_buf.size())) {
			unsigned Error = GetLastError();
			if (Error == ERROR_SUCCESS)
		  		Error = ERROR_CALL_NOT_IMPLEMENTED;
			return map_windows_error(Error);
		}

		return std::error_code();
	}

	static std::error_code real_path_from_handle(HANDLE H, std::wstring &buffer)
	{
		buffer.resize(MAX_PATH + 1);
		DWORD CountChars = ::GetFinalPathNameByHandleW(H, (LPWSTR)buffer.data(), (DWORD)buffer.size() - 1, FILE_NAME_NORMALIZED);
	  	if (CountChars > buffer.size()) {
	    	buffer.resize((size_t)CountChars);
	    	CountChars = ::GetFinalPathNameByHandleW(H, (LPWSTR)buffer.data(), (DWORD)buffer.size() - 1, FILE_NAME_NORMALIZED);
	  	}
	  	if (CountChars == 0)
	    	return map_windows_error(GetLastError());
	  	buffer.resize(CountChars);
	  	return std::error_code();
	}

	std::error_code rename(const std::string &from, const std::string &to)
	{
		std::wstring wide_from = boost::nowide::widen(from);
		std::wstring wide_to   = boost::nowide::widen(to);

		ScopedFileHandle from_handle;
		for (unsigned retry = 0; retry != 200; ++ retry) {
			if (retry != 0)
		  		::Sleep(10);
			from_handle = ::CreateFileW((LPWSTR)wide_from.data(), GENERIC_READ | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (from_handle)
		  		break;
		}
		if (! from_handle)
		{
			auto err_code = map_windows_error(GetLastError());
			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format("can not open file %1%, error: %2%") % from.c_str() % err_code.message();
			return err_code;
		}

		for (unsigned retry = 0; retry != 200; ++ retry) {
			auto errcode = rename_internal(from_handle, wide_to, true);

			if (errcode == std::error_code(ERROR_CALL_NOT_IMPLEMENTED, std::system_category())) {
		  		if (std::error_code errcode2 = real_path_from_handle(from_handle, wide_from))
		    		return errcode2;
		  		if (::MoveFileExW((LPCWSTR)wide_from.data(), (LPCWSTR)wide_to.data(), MOVEFILE_REPLACE_EXISTING))
		    		return std::error_code();
		  		return map_windows_error(GetLastError());
			}

			if (! errcode || errcode != std::errc::permission_denied)
		  		return errcode;

			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(",first rename file from %1% to %2% failed, reason: %3%") % from.c_str() % to.c_str() % errcode.message();
			ScopedFileHandle to_handle(::CreateFileW((LPCWSTR)wide_to.data(), GENERIC_READ | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, NULL));
			if (! to_handle) {
				auto errcode = map_windows_error(GetLastError());
				if (errcode == std::errc::no_such_file_or_directory)
					continue;

				BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(",open dest file %1% failed, reason: %2%") % to.c_str() % errcode.message();
				return errcode;
			}

			BY_HANDLE_FILE_INFORMATION FI;
			if (! ::GetFileInformationByHandle(to_handle, &FI))
		  		return map_windows_error(GetLastError());

			for (unsigned unique_id = 0; unique_id != 200; ++ unique_id) {
				std::wstring tmp_filename = wide_to + L".tmp" + std::to_wstring(unique_id);
				std::error_code errcode = rename_internal(to_handle, tmp_filename, false);
				if (errcode) {
					if (errcode == std::make_error_code(std::errc::file_exists) || errcode == std::make_error_code(std::errc::permission_denied)) {
						ScopedFileHandle to_handle2(::CreateFileW((LPCWSTR)wide_to.data(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
						if (! to_handle2) {
							auto errcode = map_windows_error(GetLastError());
							if (errcode == std::errc::no_such_file_or_directory)
						  		break;
							BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", line %1%, error: %2%") % __LINE__ % errcode.message();
							return errcode;
						}
						BY_HANDLE_FILE_INFORMATION FI2;
						if (! ::GetFileInformationByHandle(to_handle2, &FI2))
							return map_windows_error(GetLastError());
						if (FI.nFileIndexHigh != FI2.nFileIndexHigh || FI.nFileIndexLow != FI2.nFileIndexLow || FI.dwVolumeSerialNumber != FI2.dwVolumeSerialNumber)
							break;
						continue;
					}
					BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", line %1%, error: %2%") % __LINE__ % errcode.message();
					return errcode;
				}
				break;
			}

		}

		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", line %1%, error in the end, permission_denied") % __LINE__;
		return std::make_error_code(std::errc::permission_denied);
	}
}
#endif
std::error_code rename_file(const std::string &from, const std::string &to)
{
#ifdef _WIN32
	return WindowsSupport::rename(from, to);
#else
	if (boost::nowide::rename(from.c_str(), to.c_str()) == 0)
		return {};
	const int err = errno;
	const bool worth_retrying = err != ENOENT && err != EXDEV && err != ENOTDIR && err != EISDIR;
	if (worth_retrying && boost::nowide::remove(to.c_str()) == 0 && boost::nowide::rename(from.c_str(), to.c_str()) == 0)
		return {};
	return std::make_error_code(static_cast<std::errc>(err));
#endif
}

static std::error_code write_whole_file(const std::string &path, std::initializer_list<std::string_view> chunks, bool binary)
{
	errno = 0;
	FILE *file = boost::nowide::fopen(path.c_str(), binary ? "wb" : "w");
	if (file == nullptr)
		return std::make_error_code(errno != 0 ? static_cast<std::errc>(errno) : std::errc::io_error);
	bool ok = true;
	for (const std::string_view chunk : chunks)
		ok = ok && std::fwrite(chunk.data(), 1, chunk.size(), file) == chunk.size();
	ok = ok && std::fflush(file) == 0;
	const int err = ok ? 0 : errno;
	ok = std::fclose(file) == 0 && ok;
	if (ok)
		return {};
	return std::make_error_code(err != 0 ? static_cast<std::errc>(err) : std::errc::io_error);
}

static std::error_code write_in_place(const std::string &path, std::initializer_list<std::string_view> chunks, bool binary)
{
	static auto *mutex = new std::mutex();
	std::lock_guard<std::mutex> guard(*mutex);
	return write_whole_file(path, chunks, binary);
}

std::error_code write_file_atomically(const std::string &path, std::initializer_list<std::string_view> chunks, bool binary)
{
	boost::system::error_code bec;
	const boost::filesystem::file_status target = boost::filesystem::symlink_status(path, bec);
	const bool target_exists = ! bec && boost::filesystem::exists(target);
	if (target_exists && boost::filesystem::is_symlink(target)) {
		const boost::filesystem::path resolved = boost::filesystem::canonical(path, bec);
		if (! bec && boost::filesystem::is_regular_file(resolved, bec))
			return write_file_atomically(resolved.string(), chunks, binary);
	}
	if (target_exists && ! boost::filesystem::is_regular_file(target))
		return write_in_place(path, chunks, binary);

	static std::atomic<unsigned> counter{0};
	const std::string tmp_path = path + "." + std::to_string(get_current_pid()) + "." + std::to_string(counter++) + ".tmp";
	if (const std::error_code ec = write_whole_file(tmp_path, chunks, binary)) {
		boost::nowide::remove(tmp_path.c_str());
		if (! target_exists)
			return ec;
		BOOST_LOG_TRIVIAL(warning) << "Cannot create a temporary beside " << path << " (" << ec.message() << "); writing in place";
		return write_in_place(path, chunks, binary);
	}
#ifndef _WIN32
	if (target_exists)
		boost::filesystem::permissions(tmp_path, target.permissions(), bec);
#endif
	if (const std::error_code ec = rename_file(tmp_path, path)) {
		boost::nowide::remove(tmp_path.c_str());
		BOOST_LOG_TRIVIAL(warning) << "Cannot replace " << path << " (" << ec.message() << "); writing in place";
		return write_in_place(path, chunks, binary);
	}
	return {};
}

#ifdef __linux__
int copy_file_linux_read_write(int infile, int outfile, uintmax_t file_size)
{
    std::vector<char> buf(
    	std::clamp<size_t>(file_size + (file_size < ~static_cast<uintmax_t >(0u)),
    			   		   8u * 1024u, 256u * 1024u),
    	0);

#if defined(POSIX_FADV_SEQUENTIAL)
    ::posix_fadvise(infile, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif

    while (true) {
        ssize_t sz_read = ::read(infile, buf.data(), buf.size());
        if (sz_read == 0)
            break;
        if (sz_read < 0) {
            int err = errno;
            if (err == EINTR)
                continue;
            return err;
        }
        for (ssize_t sz_wrote = 0; sz_wrote < sz_read;) {
            ssize_t sz = ::write(outfile, buf.data() + sz_wrote, static_cast<std::size_t>(sz_read - sz_wrote));
            if (sz < 0) {
                int err = errno;
                if (err == EINTR)
                    continue;
                return err;
            }
            sz_wrote += sz;
        }
    }
    return 0;
}

bool copy_file_linux(const boost::filesystem::path &from, const boost::filesystem::path &to, boost::system::error_code &ec)
{

	struct fd_wrapper
	{
		int fd { -1 };
		fd_wrapper() = default;
		explicit fd_wrapper(int fd) throw() : fd(fd) {}
		~fd_wrapper() throw() { if (fd >= 0) ::close(fd); }
	};

	ec.clear();
  	int err = 0;

  	fd_wrapper infile, outfile;

  	while (true) {
    	infile.fd = ::open(from.c_str(), O_RDONLY | O_CLOEXEC);
    	if (infile.fd < 0) {
      		err = errno;
      		if (err == EINTR)
        		continue;
		fail:
			ec.assign(err, boost::system::system_category());
  			return false;
    	}
    	break;
  	}

	struct ::stat from_stat;
	if (::fstat(infile.fd, &from_stat) != 0) {
		fail_errno:
		err = errno;
		goto fail;
	}

  	const mode_t from_mode = from_stat.st_mode;
  	if (!S_ISREG(from_mode)) {
    	err = ENOSYS;
    	goto fail;
  	}

  	mode_t to_mode = from_mode | S_IWUSR;
  	int oflag = O_WRONLY | O_CLOEXEC | O_CREAT | O_TRUNC;

	while (true) {
	  	outfile.fd = ::open(to.c_str(), oflag, to_mode);
	  	if (outfile.fd < 0) {
	    	err = errno;
	    	if (err == EINTR)
	      		continue;
	    	goto fail;
	  	}
	  	break;
	}

	struct ::stat to_stat;
	if (::fstat(outfile.fd, &to_stat) != 0)
		goto fail_errno;

	to_mode = to_stat.st_mode;
	if (!S_ISREG(to_mode)) {
		err = ENOSYS;
		goto fail;
	}

	if (from_stat.st_dev == to_stat.st_dev && from_stat.st_ino == to_stat.st_ino) {
		err = EEXIST;
		goto fail;
	}

	{
		constexpr std::size_t max_send_size = 0x7ffff000u;
		uintmax_t offset = 0u;
		while (off_t(offset) < from_stat.st_size) {
			uintmax_t size_left = from_stat.st_size - offset;
			std::size_t size_to_copy = max_send_size;
			if (size_left < static_cast<uintmax_t>(max_send_size))
				size_to_copy = static_cast<std::size_t>(size_left);
			ssize_t sz = ::sendfile(outfile.fd, infile.fd, nullptr, size_to_copy);
			if (sz < 0) {
				err = errno;
	            if (offset == 0u) {
	                if (err == EINVAL || err == EOPNOTSUPP) {
						err = copy_file_linux_read_write(infile.fd, outfile.fd, from_stat.st_size);
						if (err < 0)
							goto fail;
	                	break;
	                }
	            }
				if (err == EINTR)
					continue;
				if (err == 0)
					break;
				goto fail;
			}
			offset += sz;
		}
	}

	if (to_mode != from_mode && ::fchmod(outfile.fd, from_mode) != 0) {
		if (platform_flavor() == PlatformFlavor::LinuxOnChromium) {
			BOOST_LOG_TRIVIAL(info) << "copy_file_linux() failed to fchmod() the output file \"" << to.string() << "\" to " << from_mode << ": " << ec.message() <<
				" This may be expected when writing to a 9p filesystem.";
		} else {
			BOOST_LOG_TRIVIAL(error) << "copy_file_linux() failed to fchmod() the output file \"" << to.string() << "\" to " << from_mode << ": " << ec.message();
		}
	}

	err = ::fdatasync(outfile.fd);
	if (err != 0)
		goto fail_errno;

	return true;
}
#endif

CopyFileResult copy_file_inner(const std::string& from, const std::string& to, std::string& error_message)
{
	const boost::filesystem::path source(from);
	const boost::filesystem::path target(to);
	static const auto perms = boost::filesystem::owner_read | boost::filesystem::owner_write | boost::filesystem::group_read | boost::filesystem::others_read;
	boost::system::error_code ec;
	boost::filesystem::permissions(target, perms, ec);
	if (ec)
		BOOST_LOG_TRIVIAL(debug) << "boost::filesystem::permisions before copy error message (this could be irrelevant message based on file system): " << ec.message();
	ec.clear();
#ifdef __linux__
	copy_file_linux(source, target, ec);
#else
	boost::filesystem::copy_file(source, target, boost::filesystem::copy_option::overwrite_if_exists, ec);
#endif
	if (ec) {
		error_message = ec.message();
        BOOST_LOG_TRIVIAL(error) << boost::format("###copy_file from %1% to %2% failed, error: %3% ")
            %source.string() %target.string() % error_message;
		return FAIL_COPY_FILE;
	}
	ec.clear();
	boost::filesystem::permissions(target, perms, ec);
	if (ec)
		BOOST_LOG_TRIVIAL(debug) << "boost::filesystem::permisions after copy error message (this could be irrelevant message based on file system): " << ec.message();
	return SUCCESS;
}

CopyFileResult copy_file(const std::string &from, const std::string &to, std::string& error_message, const bool with_check)
{
#ifdef WIN32
    const char* src_str = from.c_str();
    const char* dest_str = to.c_str();
    int src_wlen = ::MultiByteToWideChar(CP_UTF8, NULL, src_str, strlen(src_str), NULL, 0);
    wchar_t* src_wstr = new wchar_t[src_wlen + 1];
    ::MultiByteToWideChar(CP_UTF8, NULL, src_str, strlen(src_str), src_wstr, src_wlen);
    src_wstr[src_wlen] = '\0';

    int dst_wlen = ::MultiByteToWideChar(CP_UTF8, NULL, dest_str, strlen(dest_str), NULL, 0);
    wchar_t* dst_wstr = new wchar_t[dst_wlen + 1];
    ::MultiByteToWideChar(CP_UTF8, NULL, dest_str, strlen(dest_str), dst_wstr, dst_wlen);
    dst_wstr[dst_wlen] = '\0';

    CopyFileResult ret = SUCCESS;
    BOOL result = CopyFileW(src_wstr, dst_wstr, FALSE);
    if (!result) {
        DWORD errCode = GetLastError();
        error_message = "Error: " + std::to_string(errCode);
        ret = FAIL_COPY_FILE;
        goto __finished;
    }

__finished:
    if (src_wstr)
        delete[] src_wstr;
    if (dst_wstr)
        delete[] dst_wstr;

    return ret;
#else
    std::string to_temp = to + ".tmp";
    CopyFileResult ret_val = copy_file_inner(from, to_temp, error_message);
    if(ret_val == SUCCESS)
    {
        if (with_check)
            ret_val = check_copy(from, to_temp);

        if (ret_val == 0 && rename_file(to_temp, to))
            ret_val = FAIL_RENAMING;
    }
    return ret_val;
#endif
}

bool copy_framework(const std::string &from, const std::string &to)
{
    boost::filesystem::path src(from), dst(to);
    try {
        if (!boost::filesystem::is_directory(src)) {
            std::cerr << "Error: Source is not a directory: " << src << std::endl;
            return false;
        }
        boost::filesystem::create_directories(dst);
        for (boost::filesystem::directory_iterator it(src); it != boost::filesystem::directory_iterator(); ++it) {
            const auto &entry     = it->path();
            const auto  dest_path = dst / entry.filename();

            if (boost::filesystem::is_symlink(entry)) {
                boost::filesystem::copy_symlink(entry, dest_path);
            } else if (boost::filesystem::is_directory(entry)) {
                copy_framework(it->path().string(), dest_path.string());
            } else {
                boost::filesystem::copy(entry, dest_path, boost::filesystem::copy_options::overwrite_existing);
            }
        }
        return true;
    } catch (const boost::filesystem::filesystem_error &e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "Filesystem error: " << e.what();
    }
    return false;
}

CopyFileResult check_copy(const std::string &origin, const std::string &copy)
{
	boost::nowide::ifstream f1(origin, std::ifstream::in | std::ifstream::binary | std::ifstream::ate);
	boost::nowide::ifstream f2(copy, std::ifstream::in | std::ifstream::binary | std::ifstream::ate);

	if (f1.fail())
		return FAIL_CHECK_ORIGIN_NOT_OPENED;
	if (f2.fail())
		return FAIL_CHECK_TARGET_NOT_OPENED;

	std::streampos fsize = f1.tellg();
	if (fsize != f2.tellg())
		return FAIL_FILES_DIFFERENT;

	f1.seekg(0, std::ifstream::beg);
	f2.seekg(0, std::ifstream::beg);

	size_t 			  buffer_size = 8 * 1024 * 1024;
	std::vector<char> buffer_origin(buffer_size, 0);
	std::vector<char> buffer_copy(buffer_size, 0);
	do {
		f1.read(buffer_origin.data(), buffer_size);
        f2.read(buffer_copy.data(), buffer_size);
		std::streampos origin_cnt = f1.gcount();
		std::streampos copy_cnt   = f2.gcount();
		if (origin_cnt != copy_cnt ||
			(origin_cnt > 0 && std::memcmp(buffer_origin.data(), buffer_copy.data(), origin_cnt) != 0))
			return FAIL_FILES_DIFFERENT;
		fsize -= origin_cnt;
    } while (f1.good() && f2.good());

    return (f1.eof() && f2.eof() && fsize == 0) ? SUCCESS : FAIL_FILES_DIFFERENT;
}

bool is_plain_file(const boost::filesystem::directory_entry &dir_entry)
{
    if (! boost::filesystem::is_regular_file(dir_entry.status()))
        return false;
#ifdef _MSC_VER
    DWORD attributes = GetFileAttributesW(boost::nowide::widen(dir_entry.path().string()).c_str());
    return (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) == 0;
#else
    return true;
#endif
}

bool is_ini_file(const boost::filesystem::directory_entry &dir_entry)
{
    return is_plain_file(dir_entry) && strcasecmp(dir_entry.path().extension().string().c_str(), ".ini") == 0;
}

bool is_idx_file(const boost::filesystem::directory_entry &dir_entry)
{
	return is_plain_file(dir_entry) && strcasecmp(dir_entry.path().extension().string().c_str(), ".idx") == 0;
}

bool is_gcode_file(const std::string &path)
{
	return boost::iends_with(path, ".gcode");
}

bool is_json_file(const std::string& path)
{
	return boost::iends_with(path, ".json");
}

bool is_path_within_root(const std::string &rel_path, const boost::filesystem::path &root)
{
    auto is_separator = [](char c) { return c == '/' || c == '\\'; };
    if (rel_path.empty() || is_separator(rel_path.front()) || (rel_path.size() > 1 && rel_path[1] == ':'))
        return false;
    if (rel_path.find('\0') != std::string::npos)
        return false;
    for (size_t start = 0; start <= rel_path.size();) {
        size_t end = start;
        while (end < rel_path.size() && !is_separator(rel_path[end]))
            ++end;
        if (rel_path.compare(start, end - start, "..") == 0)
            return false;
        start = end + 1;
    }
    try {
        std::string root_str = boost::filesystem::weakly_canonical(root).string();
        while (!root_str.empty() && (root_str.back() == '/' || root_str.back() == boost::filesystem::path::preferred_separator))
            root_str.pop_back();
        const std::string full_str = boost::filesystem::weakly_canonical(root / rel_path).string();
        return full_str.compare(0, root_str.size(), root_str) == 0 &&
               (full_str.size() == root_str.size() || full_str[root_str.size()] == boost::filesystem::path::preferred_separator);
    } catch (const boost::filesystem::filesystem_error &) {
        return false;
    }
}

bool is_symlink_target_within_root(const std::string &link_rel_path, const std::string &target, const boost::filesystem::path &root)
{
    if (target.empty() || target.front() == '/' || target.front() == '\\' || (target.size() > 1 && target[1] == ':'))
        return false;
    const size_t sep = link_rel_path.find_last_of("/\\");
    return is_path_within_root((sep == std::string::npos ? std::string() : link_rel_path.substr(0, sep + 1)) + target, root);
}

bool is_absolute_path_within_root(const boost::filesystem::path &path, const boost::filesystem::path &root)
{
    const boost::filesystem::path rel = path.lexically_relative(root);
    return !rel.empty() && rel != "." && is_path_within_root(rel.string(), root);
}

bool is_safe_to_open_file_name(const std::string &file_name)
{
    static const std::vector<std::string> safe_extensions = {
        "jpg", "jpeg", "jfif", "pjpeg", "pjp", "png", "gif", "bmp", "webp", "tif", "tiff",
        "pdf", "txt", "md", "csv", "docx", "xlsx", "pptx",
        "stl", "obj", "3mf", "amf", "ply", "step", "stp", "iges", "igs", "dxf",
        "mp4", "mov", "webm"};
    const size_t dot = file_name.find_last_of('.');
    if (dot == std::string::npos || file_name.find_first_of("/\\:") != std::string::npos)
        return false;
    const std::string extension = boost::algorithm::to_lower_copy(file_name.substr(dot + 1));
    return std::find(safe_extensions.begin(), safe_extensions.end(), extension) != safe_extensions.end();
}

bool is_img_file(const std::string &path)
{
	return boost::iends_with(path, ".png") || boost::iends_with(path, ".svg");
}

bool is_gallery_file(const boost::filesystem::directory_entry& dir_entry, char const* type)
{
	return is_plain_file(dir_entry) && strcasecmp(dir_entry.path().extension().string().c_str(), type) == 0;
}

bool is_gallery_file(const std::string &path, char const* type)
{
	return boost::iends_with(path, type);
}

bool is_shapes_dir(const std::string& dir)
{
	return dir == sys_shapes_dir() || dir == custom_shapes_dir() || dir == handy_models_dir();
}

}

#ifdef WIN32
    #ifndef NOMINMAX
    # define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace Slic3r {

size_t get_utf8_sequence_length(const std::string& text, size_t pos)
{
	assert(pos < text.size());
	return get_utf8_sequence_length(text.c_str() + pos, text.size() - pos);
}

size_t get_utf8_sequence_length(const char *seq, size_t size)
{
	size_t length = 0;
	unsigned char c = seq[0];
	if (c < 0x80) {
		length++;
	}
	else if (c < 0xC0) {
		length++;
		while (length < size) {
			c = seq[length];
			if (c < 0x80 || c >= 0xC0) {
				break;
			}
			length++;
		}
	}
	else if (c < 0xE0) {
		if (2 > size) {
			return size;
		}
		length += 2;
	}
	else if (c < 0xF0) {
		if (3 > size) {
			return size;
		}
		length += 3;
	}
	else if (c < 0xF8) {
		if (4 > size) {
			return size;
		}
		length += 4;
	}
	else if (c < 0xFC) {
		if (5 > size) {
			return size;
		}
		length += 5;
	}
	else if (c < 0xFE) {
		if (6 > size) {
			return size;
		}
		length += 6;
	}
	else {
		length++;
	}
	return length;
}

std::string encode_path(const char *src)
{
#ifdef WIN32
    std::wstring wstr_src = boost::nowide::widen(src);
    if (wstr_src.length() == 0)
        return std::string();
    int size_needed = ::WideCharToMultiByte(0, 0, wstr_src.data(), (int)wstr_src.size(), nullptr, 0, nullptr, nullptr);
    std::string str_dst(size_needed, 0);
    ::WideCharToMultiByte(0, 0, wstr_src.data(), (int)wstr_src.size(), str_dst.data(), size_needed, nullptr, nullptr);
    return str_dst;
#else
    return src;
#endif
}

std::string decode_path(const char *src)
{
#ifdef WIN32
    int len = int(strlen(src));
    if (len == 0)
        return std::string();
    int size_needed = ::MultiByteToWideChar(0, 0, src, len, nullptr, 0);
    std::wstring wstr_dst(size_needed, 0);
    ::MultiByteToWideChar(0, 0, src, len, wstr_dst.data(), size_needed);
    return boost::nowide::narrow(wstr_dst.c_str());
#else
    return src;
#endif
}

std::string normalize_utf8_nfc(const char *src)
{
    static std::locale locale_utf8(boost::locale::generator().generate(""));
    return boost::locale::normalize(src, boost::locale::norm_nfc, locale_utf8);
}

std::vector<std::string> split_string(const std::string &str, char delimiter)
{
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string substr;

    while (std::getline(ss, substr, delimiter)) {
        result.push_back(substr);
    }
    return result;
}

namespace PerlUtils {
    std::string path_to_filename(const char *src)       { return boost::filesystem::path(src).filename().string(); }
    std::string path_to_stem(const char *src)           { return boost::filesystem::path(src).stem().string(); }
    std::string path_to_extension(const char *src)      { return boost::filesystem::path(src).extension().string(); }
    std::string path_to_parent_path(const char *src)    { return boost::filesystem::path(src).parent_path().string(); }
};


std::string string_printf(const char *format, ...)
{
    va_list args1;
    va_start(args1, format);
    va_list args2;
    va_copy(args2, args1);

    static const size_t INITIAL_LEN = 200;
    std::string buffer(INITIAL_LEN, '\0');

    int bufflen = ::vsnprintf(buffer.data(), INITIAL_LEN - 1, format, args1);

    if (bufflen >= int(INITIAL_LEN)) {
        buffer.resize(size_t(bufflen) + 1);
        ::vsnprintf(buffer.data(), buffer.size(), format, args2);
    }

    buffer.resize(bufflen);

    return buffer;
}

std::string header_slic3r_generated()
{
	return std::string(SLIC3R_APP_NAME " " SoftFever_VERSION);
}

std::string header_gcodeviewer_generated()
{
	return std::string(GCODEVIEWER_APP_NAME " " SoftFever_VERSION);
}

unsigned get_current_pid()
{
#ifdef WIN32
    return GetCurrentProcessId();
#else
    return ::getpid();
#endif
}

boost::filesystem::path download_marker_path(const boost::filesystem::path &dest_folder, const std::string &filename)
{
    return dest_folder / (filename + "." + std::to_string(get_current_pid()) + ".download");
}

bool find_unused_filename(const boost::filesystem::path &dest_folder, const std::string &filename,
                          const boost::filesystem::path &ignored_marker, std::string &result)
{
    const std::string sanitized = sanitize_filename(filename);
    const std::string extension = boost::filesystem::path(sanitized).extension().string();
    const std::string stem      = sanitized.substr(0, sanitized.size() - extension.size());
    auto is_used = [&](const std::string &name) {
        const boost::filesystem::path marker = download_marker_path(dest_folder, name);
        return boost::filesystem::exists(dest_folder / name) || (marker != ignored_marker && boost::filesystem::exists(marker));
    };
    result = sanitized;
    for (size_t version = 1; is_used(result); ++version) {
        if (version > 999)
            return false;
        result = stem + "(" + std::to_string(version) + ")" + extension;
    }
    return true;
}

std::string per_user_temp_id()
{
#ifdef WIN32
    return {};
#else
    return std::to_string(static_cast<unsigned long>(::getuid()));
#endif
}

std::string per_user_temp_dir(const std::string &base, const std::string &user_id)
{
    if (user_id.empty())
        return base;
    return base + "/orcaslicer_" + user_id;
}

std::string resolve_cli_input_path(const std::string &path)
{
    const boost::filesystem::path input(path);
    if (path.empty() || is_supported_open_protocol(path) || input.is_absolute())
        return path;

    boost::system::error_code ec;
    const boost::filesystem::path resolved = boost::filesystem::system_complete(input, ec);
    if (ec)
        return path;
    return resolved.lexically_normal().make_preferred().string();
}

std::string get_process_name(int pid)
{
#ifdef WIN32
	char name[MAX_PATH] = { 0 };
	if (pid == 0) {
		GetModuleFileNameA(NULL, name, MAX_PATH);
	}
	else {
		HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, pid);
		if (h == INVALID_HANDLE_VALUE) return {};
		GetModuleFileNameExA(h, NULL, name, MAX_PATH);
		CloseHandle(h);
	}
	char* p = name;
	while (auto q = strchr(p + 1, '\\'))
		p = q;
	return decode_path(p);
#elif defined __APPLE__
	char pathbuf[PROC_PIDPATHINFO_MAXSIZE] = { 0 };
	if (pid == 0) pid = ::getpid();
	int ret = proc_pidpath(pid, pathbuf, sizeof(pathbuf));
	if (ret <= 0) return {};
	char* p = pathbuf;
	while (auto q = strchr(p + 1, '/')) p = q;
	return p;
#else
    char pathbuf[512]  = {0};
    char proc_path[32] = "/proc/self/exe";
    if (pid != 0) { snprintf(proc_path, sizeof(proc_path), "/proc/%d/exe", pid); }
    if (readlink(proc_path, pathbuf, sizeof(pathbuf)) < 0) {
        perror(NULL);
        return {};
    }
    char *p = pathbuf;
    while (auto q = strchr(p + 1, '/')) p = q;
    return p;
#endif
}

std::string xml_escape(std::string text, bool is_marked )
{
    std::string::size_type pos = 0;
    for (;;)
    {
        pos = text.find_first_of("\"\'&<>", pos);
        if (pos == std::string::npos)
            break;

        std::string replacement;
        switch (text[pos])
        {
        case '\"': replacement = "&quot;"; break;
        case '\'': replacement = "&apos;"; break;
        case '&':  replacement = "&amp;";  break;
        case '<':  replacement = is_marked ? "<" :"&lt;"; break;
        case '>':  replacement = is_marked ? ">" :"&gt;"; break;
        default: break;
        }

        text.replace(pos, 1, replacement);
        pos += replacement.size();
    }

    return text;
}

std::string xml_escape_double_quotes_attribute_value(std::string text)
{
    std::string::size_type pos = 0;
    for (;;) {
        pos = text.find_first_of("\"&<\r\n\t", pos);
        if (pos == std::string::npos) break;

        std::string replacement;
        switch (text[pos]) {
        case '\"': replacement = "&quot;"; break;
        case '&': replacement = "&amp;"; break;
        case '<': replacement = "&lt;"; break;
        case '\r': replacement = "&#xD;"; break;
        case '\n': replacement = "&#xA;"; break;
        case '\t': replacement = "&#x9;"; break;
        default: break;
        }

        text.replace(pos, 1, replacement);
        pos += replacement.size();
    }

    return text;
}

std::string xml_unescape(std::string s)
{
	std::string ret;
	std::string::size_type i = 0;
	std::string::size_type pos = 0;
	while (i < s.size()) {
		std::string rep;
		if (s[i] == '&') {
			if (s.substr(i, 4) == "&lt;") {
				ret += s.substr(pos, i - pos) + "<";
				i += 4;
				pos = i;
			}
			else if (s.substr(i, 4) == "&gt;") {
				ret += s.substr(pos, i - pos) + ">";
				i += 4;
				pos = i;
			}
			else if (s.substr(i, 5) == "&amp;") {
				ret += s.substr(pos, i - pos) + "&";
				i += 5;
				pos = i;
			}
			else {
				++i;
			}
		}
		else {
			++i;
		}
	}

	ret += s.substr(pos);
	return ret;
}

std::string format_memsize_MB(size_t n)
{
    std::string out;
    size_t n2 = 0;
    size_t scale = 1;
    n +=  500000;
    n /= 1000000;
    while (n >= 1000) {
        n2 = n2 + scale * (n % 1000);
        n /= 1000;
        scale *= 1000;
    }
    char buf[8];
    sprintf(buf, "%d", (int)n);
    out = buf;
    while (scale != 1) {
        scale /= 1000;
        n = n2 / scale;
        n2 = n2  % scale;
        sprintf(buf, ",%03d", (int)n);
        out += buf;
    }
    return out + "MB";
}

std::string format_memsize(size_t bytes, unsigned int decimals)
{
		static constexpr const float kb = 1024.0f;
		static constexpr const float mb = 1024.0f * kb;
		static constexpr const float gb = 1024.0f * mb;
		static constexpr const float tb = 1024.0f * gb;

		const float f_bytes = static_cast<float>(bytes);
		if (f_bytes < kb)
				return std::to_string(bytes) + " bytes";
		else if (f_bytes < mb) {
				const float f_kb = f_bytes / kb;
				char buf[64];
				sprintf(buf, "%.*f", decimals, f_kb);
				return std::to_string(bytes) + " bytes (" + std::string(buf) + "KB)";
		}
		else if (f_bytes < gb) {
				const float f_mb = f_bytes / mb;
				char buf[64];
				sprintf(buf, "%.*f", decimals, f_mb);
				return std::to_string(bytes) + " bytes (" + std::string(buf) + "MB)";
		}
		else if (f_bytes < tb) {
				const float f_gb = f_bytes / gb;
				char buf[64];
				sprintf(buf, "%.*f", decimals, f_gb);
				return std::to_string(bytes) + " bytes (" + std::string(buf) + "GB)";
		}
		else {
				const float f_tb = f_bytes / tb;
				char buf[64];
				sprintf(buf, "%.*f", decimals, f_tb);
				return std::to_string(bytes) + " bytes (" + std::string(buf) + "TB)";
		}
}

std::string format_diameter_to_str(double diameter, int precision)
{
    double candidates[] = {0.2, 0.4, 0.6, 0.8};
    double best = *std::min_element(std::begin(candidates), std::end(candidates), [diameter](double a, double b) { return std::abs(a - diameter) < std::abs(b - diameter); });
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << best;
    return oss.str();
}

void trim_free_memory()
{
    scalable_allocation_command(TBBMALLOC_CLEAN_ALL_BUFFERS, nullptr);
#if defined(__GLIBC__)
    malloc_trim(0);
#elif defined(__APPLE__)
    malloc_zone_pressure_relief(nullptr, 0);
#elif defined(_WIN32)
    _heapmin();
#endif
}

std::string log_memory_info(bool ignore_loglevel)
{
    std::string out;
    if (ignore_loglevel || logSeverity <= boost::log::trivial::info) {
#ifdef WIN32
    #ifndef PROCESS_MEMORY_COUNTERS_EX
        typedef struct _PROCESS_MEMORY_COUNTERS_EX {
          DWORD  cb;
          DWORD  PageFaultCount;
          SIZE_T PeakWorkingSetSize;
          SIZE_T WorkingSetSize;
          SIZE_T QuotaPeakPagedPoolUsage;
          SIZE_T QuotaPagedPoolUsage;
          SIZE_T QuotaPeakNonPagedPoolUsage;
          SIZE_T QuotaNonPagedPoolUsage;
          SIZE_T PagefileUsage;
          SIZE_T PeakPagefileUsage;
          SIZE_T PrivateUsage;
        } PROCESS_MEMORY_COUNTERS_EX, *PPROCESS_MEMORY_COUNTERS_EX;
    #endif

        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
            out = " WorkingSet: " + format_memsize_MB(pmc.WorkingSetSize) + "; PrivateBytes: " + format_memsize_MB(pmc.PrivateUsage) + "; Pagefile(peak): " + format_memsize_MB(pmc.PagefileUsage) + "(" + format_memsize_MB(pmc.PeakPagefileUsage) + ")";
        else
            out += " Used memory: N/A";
#elif defined(__linux__) or defined(__APPLE__)
    #ifdef __APPLE__
        struct mach_task_basic_info info;
        mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
        out += " Resident memory: ";
        if ( task_info( mach_task_self( ), MACH_TASK_BASIC_INFO, (task_info_t)&info, &infoCount ) == KERN_SUCCESS )
            out += format_memsize_MB((size_t)info.resident_size);
        else
            out += "N/A";
    #else
        size_t tSize = 0, resident = 0, share = 0;
        std::ifstream buffer("/proc/self/statm");
        if (buffer && (buffer >> tSize >> resident >> share)) {
            size_t page_size = (size_t)sysconf(_SC_PAGE_SIZE);
            size_t rss = resident * page_size;
            out += " Resident memory: " + format_memsize_MB(rss);
            out += "; Shared memory: " + format_memsize_MB(share * page_size);
            out += "; Private memory: " + format_memsize_MB(rss - share * page_size);
        }
        else
            out += " Used memory: N/A";
    #endif
        out += "; Peak memory usage: ";
        rusage memory_info;
        if (getrusage(RUSAGE_SELF, &memory_info) == 0)
        {
            size_t peak_mem_usage = (size_t)memory_info.ru_maxrss;
            #ifdef __linux__
                peak_mem_usage *= 1024;
            #endif
            out += format_memsize_MB(peak_mem_usage);
        }
        else
            out += "N/A";
#endif
    }
    return out;
}

double process_cpu_seconds()
{
#ifdef _WIN32
    FILETIME creation, exit, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        return -1.;
    auto seconds = [](const FILETIME &t) { return double((uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime) * 1e-7; };
    return seconds(kernel) + seconds(user);
#else
    rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        return -1.;
    return double(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) + 1e-6 * double(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec);
#endif
}

size_t total_physical_memory()
{
#if defined(_WIN32) && (defined(__CYGWIN__) || defined(__CYGWIN32__))
	MEMORYSTATUS status;
	status.dwLength = sizeof(status);
	GlobalMemoryStatus( &status );
	return (size_t)status.dwTotalPhys;
#elif defined(_WIN32)
	MEMORYSTATUSEX status;
	status.dwLength = sizeof(status);
	GlobalMemoryStatusEx( &status );
	return (size_t)status.ullTotalPhys;
#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))

#if defined(CTL_HW) && (defined(HW_MEMSIZE) || defined(HW_PHYSMEM64))
	int mib[2];
	mib[0] = CTL_HW;
#if defined(HW_MEMSIZE)
	mib[1] = HW_MEMSIZE;
#elif defined(HW_PHYSMEM64)
	mib[1] = HW_PHYSMEM64;
#endif
	int64_t size = 0;
	size_t len = sizeof( size );
	if ( sysctl( mib, 2, &size, &len, NULL, 0 ) == 0 )
		return (size_t)size;
	return 0L;

#elif defined(_SC_AIX_REALMEM)
	return (size_t)sysconf( _SC_AIX_REALMEM ) * (size_t)1024L;

#elif defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
	return (size_t)sysconf( _SC_PHYS_PAGES ) *
		(size_t)sysconf( _SC_PAGESIZE );

#elif defined(_SC_PHYS_PAGES) && defined(_SC_PAGE_SIZE)
	return (size_t)sysconf( _SC_PHYS_PAGES ) *
		(size_t)sysconf( _SC_PAGE_SIZE );

#elif defined(CTL_HW) && (defined(HW_PHYSMEM) || defined(HW_REALMEM))
	int mib[2];
	mib[0] = CTL_HW;
#if defined(HW_REALMEM)
	mib[1] = HW_REALMEM;
#elif defined(HW_PYSMEM)
	mib[1] = HW_PHYSMEM;
#endif
	unsigned int size = 0;
	size_t len = sizeof( size );
	if ( sysctl( mib, 2, &size, &len, NULL, 0 ) == 0 )
		return (size_t)size;
	return 0L;
#endif

#else
	return 0L;
#endif
}

bool makedir(const std::string path) {
#ifdef WIN32
	if (_access(path.c_str(), 0) != 0)
		return _mkdir(path.c_str()) == 0;
#elif __linux__
	if (opendir(path.c_str()) == NULL) {
		return mkdir(path.c_str(), 0777) == 0;
	}
#else
#endif
	return true;
}

bool bbl_calc_md5(std::string &filename, std::string &md5_out)
{
    // Boost's MD5 rather than OpenSSL's keeps libslic3r free of the GPL-incompatible OpenSSL licence.
    boost::uuids::detail::md5 md5;
    boost::nowide::ifstream   ifs(filename, std::ios::binary);
    std::string               buf(64 * 1024, 0);
    boost::filesystem::file_size(filename);
    while (ifs) {
        ifs.read(buf.data(), buf.size());
        md5.process_bytes(buf.data(), size_t(ifs.gcount()));
    }
    boost::uuids::detail::md5::digest_type digest;
    md5.get_digest(digest);
    static_assert(sizeof(digest) == 16, "an MD5 digest is 16 bytes");
    constexpr int elements = int(sizeof(digest) / sizeof(digest[0]));
    constexpr int width    = 32 / elements;
    char md5_str[33];
    for (int j = 0; j < elements; j++) { sprintf(&md5_str[j * width], "%0*X", width, (unsigned int) digest[j]); }
    md5_out = std::string(md5_str);
    return true;
}

void copy_directory_recursively(const boost::filesystem::path& source,
                                const boost::filesystem::path& target,
                                std::function<bool(const std::string)> filter,
                                bool merge_mode)
{
    BOOST_LOG_TRIVIAL(info) << Slic3r::format("copy_directory_recursively %1% -> %2%", source, target);
    std::string error_message;

    if (!merge_mode && boost::filesystem::exists(target))
        boost::filesystem::remove_all(target);
    boost::filesystem::create_directories(target);
    for (auto &dir_entry : boost::filesystem::directory_iterator(source))
    {
        std::string source_file = dir_entry.path().string();
        std::string name = dir_entry.path().filename().string();
        std::string target_file = target.string() + "/" + name;

        if (boost::filesystem::is_directory(dir_entry)) {
            const auto target_path = target / name;
            copy_directory_recursively(dir_entry, target_path, filter, merge_mode);
        }
        else {
			if(filter && filter(name))
				continue;
            CopyFileResult cfr = copy_file(source_file, target_file, error_message, false);
            if (cfr != CopyFileResult::SUCCESS) {
                BOOST_LOG_TRIVIAL(error) << "Copying failed(" << cfr << "): " << error_message;
                throw Slic3r::CriticalException(Slic3r::format(
                    ("Copying directory %1% to %2% failed: %3%"),
                    source, target, error_message));
            }
        }
    }
    return;
}

static bool cache_covers(const Semver& cache_ver, const Semver& profile_ver)
{
    return cache_ver.valid() && (! profile_ver.valid() || cache_ver >= profile_ver);
}

bool is_vendor_installed(const std::string& vendor)
{
    const boost::filesystem::path dir = boost::filesystem::path(data_dir()) / PRESET_SYSTEM_DIR;
    return boost::filesystem::exists(dir / (vendor + ".json"))
        || VendorCacheFile::usable_version((dir / (vendor + ".opc")).string(), vendor).valid();
}

Semver installed_vendor_version(const std::string& vendor)
{
    const boost::filesystem::path dir  = boost::filesystem::path(data_dir()) / PRESET_SYSTEM_DIR;
    const boost::filesystem::path json = dir / (vendor + ".json");
    const Semver from_json  = boost::filesystem::exists(json) ? get_version_from_json(json.string()) : Semver();
    const Semver from_cache = VendorCacheFile::usable_version((dir / (vendor + ".opc")).string(), vendor);
    return cache_covers(from_cache, from_json) ? from_cache : from_json;
}

void remove_installed_vendor(const std::string& vendor)
{
    const boost::filesystem::path dir = boost::filesystem::path(data_dir()) / PRESET_SYSTEM_DIR;
    boost::filesystem::remove(dir / (vendor + ".json"));
    boost::filesystem::remove(dir / (vendor + ".opc"));
    if (boost::filesystem::exists(dir / vendor))
        boost::filesystem::remove_all(dir / vendor);
}

std::set<std::string> vendor_names_in(const boost::filesystem::path& dir)
{
    std::set<std::string> names;
    for (auto& dir_entry : boost::filesystem::directory_iterator(dir)) {
        const auto& path = dir_entry.path();
        if (Slic3r::is_json_file(path.string()) || path.extension() == ".opc")
            names.insert(path.stem().string());
    }
    return names;
}

static Semver installable_cache_version(const boost::filesystem::path& dir, const std::string& vendor)
{
    const auto cache_ver = Semver::parse(VendorCacheFile::peek_version((dir / (vendor + ".opc")).string(), vendor));
    if (! cache_ver)
        return Semver::invalid();
    const Semver profile_ver = get_version_from_json((dir / (vendor + ".json")).string());
    return cache_covers(*cache_ver, profile_ver) ? *cache_ver : Semver::invalid();
}

Semver resource_vendor_version(const std::string& vendor)
{
    const boost::filesystem::path dir = boost::filesystem::path(resources_dir()) / "profiles";
    const Semver ver = installable_cache_version(dir, vendor);
    return ver.valid() ? ver : get_version_from_json((dir / (vendor + ".json")).string());
}

bool install_vendor_bundles_from_resources(
    const std::vector<std::string>& bundle_names,
    const std::string& resource_subdir,
    const std::string& data_subdir)
{
    namespace fs = boost::filesystem;

    fs::path rsrc_path = fs::path(Slic3r::resources_dir()) / resource_subdir;
    fs::path vendor_path = fs::path(Slic3r::data_dir()) / data_subdir;

    BOOST_LOG_TRIVIAL(info) << "Installing " << bundle_names.size() << " bundles from resources...";

    bool all_installed = true;

    for (const auto &bundle : bundle_names) {
        try {
            if (bundle.empty()) {
                BOOST_LOG_TRIVIAL(warning) << "Refusing to install a bundle with no name";
                all_installed = false;
                continue;
            }

            auto path_in_rsrc = (rsrc_path / bundle).replace_extension(".json");
            auto path_in_vendors = (vendor_path / bundle).replace_extension(".json");
            auto cache_in_rsrc = (rsrc_path / bundle).replace_extension(".opc");
            auto cache_in_vendors = (vendor_path / bundle).replace_extension(".opc");

            if (!fs::exists(path_in_rsrc) && !fs::exists(cache_in_rsrc)) {
                BOOST_LOG_TRIVIAL(warning) << "Bundle not found in resources: " << bundle;
                all_installed = false;
                continue;
            }

            if (!fs::exists(vendor_path))
                fs::create_directories(vendor_path);

            std::string error_message;
            bool installed_cache = false;
            if (installable_cache_version(rsrc_path, bundle).valid()) {
                installed_cache = copy_file(cache_in_rsrc.string(), cache_in_vendors.string(), error_message, false) == CopyFileResult::SUCCESS;
                if (! installed_cache) {
                    BOOST_LOG_TRIVIAL(warning) << "Failed to copy " << bundle << ".opc: " << error_message;
                } else if (! VendorCacheFile::usable_version(cache_in_vendors.string(), bundle).valid()) {
                    BOOST_LOG_TRIVIAL(warning) << "Installed cache for " << bundle << " cannot be read; installing its profile instead";
                    boost::system::error_code ec;
                    fs::remove(cache_in_vendors, ec);
                    installed_cache = false;
                }
            }

            if (! installed_cache) {
                CopyFileResult cfr = copy_file(path_in_rsrc.string(), path_in_vendors.string(), error_message, false);
                if (cfr != CopyFileResult::SUCCESS) {
                    BOOST_LOG_TRIVIAL(error) << "Failed to copy " << bundle << ".json: " << error_message;
                    all_installed = false;
                    continue;
                }
                boost::system::error_code ec;
                fs::remove(cache_in_vendors, ec);
            } else {
                boost::system::error_code ec;
                fs::remove(path_in_vendors, ec);
                if (ec)
                    BOOST_LOG_TRIVIAL(warning) << "Could not remove the superseded profile " << path_in_vendors.string() << ": " << ec.message();
            }

            auto dir_in_rsrc = rsrc_path / bundle;
            auto dir_in_vendors = vendor_path / bundle;

            if (fs::exists(dir_in_vendors))
                fs::remove_all(dir_in_vendors);

            if (! installed_cache && fs::exists(dir_in_rsrc) && fs::is_directory(dir_in_rsrc)) {
                fs::create_directories(dir_in_vendors);

                auto file_filter = [](const std::string name) -> bool {
                    return boost::iends_with(name, ".stl") ||
                           boost::iends_with(name, ".png") ||
                           boost::iends_with(name, ".svg") ||
                           boost::iends_with(name, ".jpeg") ||
                           boost::iends_with(name, ".jpg") ||
                           boost::iends_with(name, ".3mf");
                };

                copy_directory_recursively(dir_in_rsrc, dir_in_vendors, file_filter);
            }

            BOOST_LOG_TRIVIAL(info) << "Successfully installed bundle: " << bundle;

        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Exception installing bundle " << bundle << ": " << e.what();
            all_installed = false;
        }
    }

    return all_installed;
}

void save_string_file(const boost::filesystem::path& p, const std::string& str)
{
    boost::nowide::ofstream file;
    file.exceptions(std::ios_base::failbit | std::ios_base::badbit);
    file.open(p.generic_string(), std::ios_base::binary);
    file.write(str.c_str(), str.size());
}

void load_string_file(const boost::filesystem::path& p, std::string& str)
{
    boost::nowide::ifstream file;
    file.exceptions(std::ios_base::failbit | std::ios_base::badbit);
    file.open(p.generic_string(), std::ios_base::binary);
    std::size_t sz = static_cast<std::size_t>(boost::filesystem::file_size(p));
    str.resize(sz, '\0');
    file.read(&str[0], sz);
}

bool check_layer_id_pattern(const std::string& pattern, int layer_id){
    if (pattern.empty() || layer_id < 0)
        return false;

	layer_id++;

    std::string p; p.reserve(pattern.size());
    for (char c : pattern) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;
        p.push_back(c);
    }
    if (!p.empty() && (p.front() == '"' || p.front() == '\''))
        p.erase(p.begin());
    if (!p.empty() && (p.back() == '"' || p.back() == '\''))
        p.pop_back();
    if (p.empty())
        return false;

    if (p.find(',') != std::string::npos) {
        size_t start = 0;
        while (start < p.size()) {
            size_t end = p.find(',', start);
            std::string token = p.substr(start, (end == std::string::npos) ? std::string::npos : end - start);
            if (!token.empty()) {
                try {
                    size_t hash_pos_token = token.find('#');
                    if (hash_pos_token == std::string::npos) {
                        int value = std::stoi(token);
                        if (value == layer_id)
                            return true;
                    } else {
                        int base_layer = std::stoi(token.substr(0, hash_pos_token));
                        std::string count_str = token.substr(hash_pos_token + 1);
                        int local_count = 1;
                        if (!count_str.empty())
                            local_count = std::stoi(count_str);
                        if (base_layer > 0 && local_count > 0) {
                            if (layer_id >= base_layer && layer_id < base_layer + local_count)
                                return true;
                        }
                    }
                } catch (...) {
                }
            }
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
        return false;
    }

    int interval = 0;
    int count    = 1;
    size_t hash_pos = p.find('#');
    try {
        if (hash_pos == std::string::npos) {
            interval = std::stoi(p);
        } else {
            interval = std::stoi(p.substr(0, hash_pos));
            std::string count_str = p.substr(hash_pos + 1);
            if (!count_str.empty())
                count = std::stoi(count_str);
        }
    } catch (...) {
        return false;
    }

    if (interval <= 0 || count <= 0)
        return false;

    if (layer_id < interval)
        return false;
    int mod = layer_id % interval;
    return mod >= 0 && mod < count;
}


};
