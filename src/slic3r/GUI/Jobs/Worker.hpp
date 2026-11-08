#ifndef PRUSALSICER_WORKER_HPP
#define PRUSALSICER_WORKER_HPP

#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

#include "Job.hpp"

namespace Slic3r { namespace GUI {

class Worker {
public:
    virtual bool push(std::shared_ptr<Job> job) = 0;

    virtual bool is_idle() const = 0;

    virtual void cancel() = 0;

    virtual void cancel_all() = 0;

    virtual void process_events() = 0;

    virtual bool wait_for_current_job(unsigned timeout_ms = 0) = 0;

    virtual bool wait_for_idle(unsigned timeout_ms = 0) = 0;

    virtual ~Worker() = default;
};

template<class Fn> constexpr bool IsProcessFn = std::is_invocable_v<Fn, Job::Ctl&>;
template<class Fn> constexpr bool IsFinishFn  = std::is_invocable_v<Fn, bool, std::exception_ptr&>;

template<class ProcessFn, class FinishFn,
         class = std::enable_if_t<IsProcessFn<ProcessFn>>,
         class = std::enable_if_t<IsFinishFn<FinishFn>> >
bool queue_job(Worker &w, ProcessFn fn, FinishFn finishfn)
{
    struct LambdaJob: Job {
        ProcessFn fn;
        FinishFn  finishfn;

        LambdaJob(ProcessFn pfn, FinishFn ffn)
            : fn{std::move(pfn)}, finishfn{std::move(ffn)}
        {}

        void process(Ctl &ctl) override { fn(ctl); }
        void finalize(bool canceled, std::exception_ptr &eptr) override
        {
            finishfn(canceled, eptr);
        }
    };

    auto j = std::make_shared<LambdaJob>(std::move(fn), std::move(finishfn));
    return w.push(std::move(j));
}

template<class ProcessFn, class = std::enable_if_t<IsProcessFn<ProcessFn>>>
bool queue_job(Worker &w, ProcessFn fn)
{
    return queue_job(w, std::move(fn), [](bool, std::exception_ptr &) {});
}

inline bool queue_job(Worker &w, std::shared_ptr<Job> j)
{
    return w.push(std::move(j));
}

template<class...Args> bool replace_job(Worker &w, Args&& ...args)
{
    w.cancel_all();
    return queue_job(w, std::forward<Args>(args)...);
}

inline bool stop_current_job(Worker &w, unsigned timeout_ms = 0)
{
    w.cancel();
    return w.wait_for_current_job(timeout_ms);
}

inline bool stop_queue(Worker &w, unsigned timeout_ms = 0)
{
    w.cancel_all();
    return w.wait_for_idle(timeout_ms);
}

}}

#endif
