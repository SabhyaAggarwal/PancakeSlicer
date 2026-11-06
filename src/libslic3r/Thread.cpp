#ifdef _WIN32
	#include <windows.h>
	#include <boost/nowide/convert.hpp>
#else

	#include <pthread.h>
#endif

#include <optional>
#include <string>
#include <cstddef>
#include <cassert>
#include <sstream>
#include <locale.h>
#include <clocale>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>

#include "Thread.hpp"
#include <cstring>

namespace Slic3r {

#ifdef _WIN32



typedef HRESULT(__stdcall* SetThreadDescriptionType)(HANDLE, PCWSTR);
typedef HRESULT(__stdcall* GetThreadDescriptionType)(HANDLE, PWSTR*);

static bool 					s_SetGetThreadDescriptionInitialized = false;
static HMODULE					s_hKernel32 = nullptr;
static SetThreadDescriptionType s_fnSetThreadDescription = nullptr;
static GetThreadDescriptionType	s_fnGetThreadDescription = nullptr;


template<typename Fn> static Fn load_proc(HMODULE module, const char* name) {
	return reinterpret_cast<Fn>(reinterpret_cast<void(*)()>(::GetProcAddress(module, name)));
}

static bool WindowsGetSetThreadNameAPIInitialize()
{
	if (! s_SetGetThreadDescriptionInitialized) {


		s_hKernel32 = LoadLibraryW(L"Kernel32.dll");
		if (s_hKernel32) {
			s_fnSetThreadDescription = load_proc<SetThreadDescriptionType>(s_hKernel32, "SetThreadDescription");
			s_fnGetThreadDescription = load_proc<GetThreadDescriptionType>(s_hKernel32, "GetThreadDescription");
		}
		s_SetGetThreadDescriptionInitialized = true;
	}
	return s_fnSetThreadDescription && s_fnGetThreadDescription;
}

#ifndef NDEBUG

	static constexpr DWORD MSVC_SEH_EXCEPTION_NAME_THREAD = 0x406D1388;

#pragma pack(push,8)
	typedef struct tagTHREADNAME_INFO
	{
		DWORD  dwType;
		LPCSTR szName;
		DWORD  dwThreadID;
		DWORD  dwFlags;
	} THREADNAME_INFO;
#pragma pack(pop)

	static void WindowsSetThreadNameSEH(HANDLE hThread, const char* thread_name)
	{
		THREADNAME_INFO info;
		info.dwType 	= 0x1000;
		info.szName 	= thread_name;
		info.dwThreadID = ::GetThreadId(hThread);
		info.dwFlags 	= 0;
		__try {
			RaiseException(MSVC_SEH_EXCEPTION_NAME_THREAD, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}
#endif

static bool WindowsSetThreadName(HANDLE hThread, const char *thread_name)
{
	if (! WindowsGetSetThreadNameAPIInitialize()) {
#ifdef NDEBUG
		return false;
#else


		WindowsSetThreadNameSEH(hThread, thread_name);
		return true;
#endif
	}

	size_t len = strlen(thread_name);
	if (len < 1024) {

		wchar_t buf[1024];
		s_fnSetThreadDescription(hThread, boost::nowide::widen(buf, 1024, thread_name));
	} else {

		s_fnSetThreadDescription(hThread, boost::nowide::widen(thread_name).c_str());
	}
	return true;
}

bool set_thread_name(std::thread &thread, const char *thread_name)
{
    return WindowsSetThreadName(static_cast<HANDLE>(thread.native_handle()), thread_name);
}

bool set_thread_name(boost::thread &thread, const char *thread_name)
{
    return WindowsSetThreadName(static_cast<HANDLE>(thread.native_handle()), thread_name);
}

bool set_current_thread_name(const char *thread_name)
{
    return WindowsSetThreadName(::GetCurrentThread(), thread_name);
}

std::optional<std::string> get_current_thread_name()
{
	if (! WindowsGetSetThreadNameAPIInitialize())
		return std::nullopt;

	wchar_t *ptr = nullptr;
	s_fnGetThreadDescription(::GetCurrentThread(), &ptr);
	return (ptr == nullptr) ? std::string() : boost::nowide::narrow(ptr);
}

#else

#ifdef __APPLE__


bool set_thread_name(std::thread &thread, const char *thread_name)
{


	return false;
}

bool set_thread_name(boost::thread &thread, const char *thread_name)
{


	return false;
}

bool set_current_thread_name(const char *thread_name)
{
	pthread_setname_np(thread_name);
	return true;
}

std::optional<std::string> get_current_thread_name()
{



	return std::nullopt;
}

#else


bool set_thread_name(std::thread &thread, const char *thread_name)
{
    pthread_setname_np(thread.native_handle(), thread_name);
	return true;
}

bool set_thread_name(boost::thread &thread, const char *thread_name)
{
    pthread_setname_np(thread.native_handle(), thread_name);
	return true;
}

bool set_current_thread_name(const char *thread_name)
{
	pthread_setname_np(pthread_self(), thread_name);
	return true;
}

std::optional<std::string> get_current_thread_name()
{
	char buf[16];
	return std::string(pthread_getname_np(pthread_self(), buf, 16) == 0 ? buf : "");
}

#endif

#endif


static boost::thread::id g_main_thread_id;

void save_main_thread_id()
{
	g_main_thread_id = boost::this_thread::get_id();
}


boost::thread::id get_main_thread_id()
{
	return g_main_thread_id;
}


bool is_main_thread_active()
{
	return get_main_thread_id() == boost::this_thread::get_id();
}



void name_tbb_thread_pool_threads_set_locale()
{
	static bool initialized = false;
	if (initialized)
		return;
	initialized = true;




	const size_t nthreads_hw = tbb::this_task_arena::max_concurrency();
	size_t       nthreads    = nthreads_hw;

#ifdef SLIC3R_PROFILE

	disable_multi_threading();
	nthreads = 1;
#endif

	size_t                  nthreads_running(0);
	std::condition_variable cv;
	std::mutex				cv_m;
	auto					master_thread_id = std::this_thread::get_id();
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, nthreads, 1),
        [&nthreads_running, nthreads, &master_thread_id, &cv, &cv_m](const tbb::blocked_range<size_t> &range) {
            assert(range.begin() + 1 == range.end());
			if (std::unique_lock<std::mutex> lk(cv_m);  ++nthreads_running == nthreads) {
				lk.unlock();


                cv.notify_all();
            } else {

			    cv.wait(lk, [&nthreads_running, nthreads]{return nthreads_running == nthreads;});
            }
            auto thread_id = std::this_thread::get_id();
			if (thread_id == master_thread_id) {

				assert(range.begin() == 0);
			} else {
				assert(range.begin() > 0);
				std::ostringstream name;
		        name << "slic3r_tbb_" << range.begin();
		        set_current_thread_name(name.str().c_str());

#ifdef _WIN32
			    _configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
			    std::setlocale(LC_ALL, "C");
#else


				uselocale(newlocale(
#ifdef __APPLE__
					LC_ALL_MASK
#else
					LC_ALL
#endif
					, "C", nullptr));
#endif
            }
        });
}

}
