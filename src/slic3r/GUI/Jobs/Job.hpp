#ifndef JOB_HPP
#define JOB_HPP

#include <atomic>
#include <exception>
#include <functional>
#include <future>

#include <string>
#include <wx/window.h>

#include "libslic3r/libslic3r.h"
#include "ProgressIndicator.hpp"

namespace Slic3r { namespace GUI {

class Job {
public:

    enum JobPrepareState {
        PREPARE_STATE_DEFAULT = 0,
        PREPARE_STATE_MENU = 1,
    };

    class Ctl {
    public:
        virtual ~Ctl() = default;

        virtual void update_status(int st, const std::string &msg = "") = 0;

        virtual bool was_canceled() const = 0;

        virtual void clear_percent()                                                                                             = 0;
        virtual void show_error_info(const std::string &msg, int code, const std::string &description, const std::string &extra) = 0;

        virtual std::future<void> call_on_main_thread(std::function<void()> fn) = 0;
    };

    virtual ~Job() = default;

    virtual void process(Ctl &ctl) = 0;

    virtual void finalize(bool , std::exception_ptr &) {}
};

}}

#endif
