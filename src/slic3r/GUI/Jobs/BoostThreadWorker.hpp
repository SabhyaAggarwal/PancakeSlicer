#ifndef BOOSTTHREADWORKER_HPP
#define BOOSTTHREADWORKER_HPP

#include <atomic>
#include <boost/variant.hpp>

#include "Worker.hpp"

#include <exception>
#include <functional>
#include <future>
#include <boost/variant/variant.hpp>
#include <libslic3r/Thread.hpp>
#include <boost/log/trivial.hpp>
#include "slic3r/GUI/Jobs/Job.hpp"
#include <memory>
#include <string>
#include <utility>
#include "slic3r/GUI/Jobs/ProgressIndicator.hpp"

#include "ThreadSafeQueue.hpp"
#include "slic3r/GUI/GUI.hpp"

namespace Slic3r { namespace GUI {

class BoostThreadWorker : public Worker, private Job::Ctl
{
    struct JobEntry
    {
        std::shared_ptr<Job> job;
        bool                 canceled = false;
        std::exception_ptr   eptr     = nullptr;
    };

    struct StatusInfo { int status; std::string msg; };

    struct MainThreadCallData
    {
        std::function<void()> fn;
        std::promise<void>    promise;
    };

    struct EmptyMessage {};

    class WorkerMessage
    {
    public:
        enum MsgType { Empty, Status, Finalize, MainThreadCall };

    private:
        boost::variant<EmptyMessage, StatusInfo, JobEntry, MainThreadCallData> m_data;

    public:
        WorkerMessage() = default;
        WorkerMessage(int s, std::string txt)
            : m_data{StatusInfo{s, std::move(txt)}}
        {}
        WorkerMessage(JobEntry &&entry) : m_data{std::move(entry)} {}
        WorkerMessage(MainThreadCallData fn) : m_data{std::move(fn)} {}

        int get_type () const { return m_data.which(); }

        void deliver(BoostThreadWorker &runner);
    };

    using JobQueue     = ThreadSafeQueueSPSC<JobEntry>;
    using MessageQueue = ThreadSafeQueueSPSC<WorkerMessage>;

    boost::thread                      m_thread;
    std::atomic<bool>                  m_running{false}, m_canceled{false};
    std::shared_ptr<ProgressIndicator> m_progress;
    JobQueue     m_input_queue;
    MessageQueue m_output_queue;
    std::string  m_name;

    void run();

    bool join(int timeout_ms = 0);

protected:

    void update_status(int st, const std::string &msg = "") override;

    bool was_canceled() const override { return m_canceled.load(); }

    std::future<void> call_on_main_thread(std::function<void()> fn) override;

public:
    explicit BoostThreadWorker(std::shared_ptr<ProgressIndicator> pri,
                               boost::thread::attributes &        attr,
                               const char *                       name = "");

    explicit BoostThreadWorker(std::shared_ptr<ProgressIndicator> pri,
                               boost::thread::attributes &&       attr,
                               const char *                       name = "")
        : BoostThreadWorker{std::move(pri), attr, name}
    {}

    explicit BoostThreadWorker(std::shared_ptr<ProgressIndicator> pri,
                               const char *                       name = "")
        : BoostThreadWorker{std::move(pri), {}, name}
    {}

    ~BoostThreadWorker();

    BoostThreadWorker(const BoostThreadWorker &) = delete;
    BoostThreadWorker(BoostThreadWorker &&)      = delete;
    BoostThreadWorker &operator=(const BoostThreadWorker &) = delete;
    BoostThreadWorker &operator=(BoostThreadWorker &&) = delete;

    bool push(std::shared_ptr<Job> job) override;

    bool is_idle() const override
    {
        return m_input_queue.empty() && !m_running.load() && m_output_queue.empty();
    }

    void cancel() override { m_canceled.store(true); }
    void cancel_all() override { m_input_queue.clear(); cancel(); }

    ProgressIndicator * get_pri() { return m_progress.get(); }
    const ProgressIndicator * get_pri() const  { return m_progress.get(); }

    void clear_percent() override
    {
        if (m_progress) {
            m_progress->clear_percent();
        }
    }

    void show_error_info(const std::string &msg, int code, const std::string &description, const std::string &extra) override
    {
        if (m_progress) {
            m_progress->show_error_info(from_u8(msg), code, from_u8(description), from_u8(extra));
        }
    }

    void process_events() override;
    bool wait_for_current_job(unsigned timeout_ms = 0) override;
    bool wait_for_idle(unsigned timeout_ms = 0) override;

};

}}

#endif
