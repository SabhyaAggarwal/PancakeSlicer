#ifndef GUI_THREAD_HPP
#define GUI_THREAD_HPP

#include <optional>
#include <utility>
#include <string>
#include <thread>
#include <boost/thread.hpp>

namespace Slic3r {

bool set_thread_name(std::thread &thread, const char *thread_name);
inline bool set_thread_name(std::thread &thread, const std::string &thread_name) { return set_thread_name(thread, thread_name.c_str()); }
bool set_thread_name(boost::thread &thread, const char *thread_name);
inline bool set_thread_name(boost::thread &thread, const std::string &thread_name) { return set_thread_name(thread, thread_name.c_str()); }
bool set_current_thread_name(const char *thread_name);
inline bool set_current_thread_name(const std::string &thread_name) { return set_current_thread_name(thread_name.c_str()); }

void save_main_thread_id();
boost::thread::id get_main_thread_id();
bool is_main_thread_active();

std::optional<std::string> get_current_thread_name();

void name_tbb_thread_pool_threads_set_locale();

template<class Fn>
inline boost::thread create_thread(boost::thread::attributes &attrs, Fn &&fn)
{
    attrs.set_stack_size(16 * 1024 * 1024);
    return boost::thread{attrs, std::forward<Fn>(fn)};
}

template<class Fn> inline boost::thread create_thread(Fn &&fn)
{
    boost::thread::attributes attrs;
    return create_thread(attrs, std::forward<Fn>(fn));    
}

}

#endif
