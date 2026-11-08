#ifndef slic3r_GUI_ProcessRunner_hpp_
#define slic3r_GUI_ProcessRunner_hpp_

#include <boost/process/pipe.hpp>
#include <wx/event.h>
#include <wx/timer.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <functional>

#include <boost/process.hpp>

namespace boost::process { class child; }

namespace Slic3r { namespace GUI {

class ProcessRunner : public wxEvtHandler
{
public:
    ProcessRunner();
    ~ProcessRunner() override;

    struct OutputLine {
        std::string text;
        bool is_stderr;
    };
    using OutputCallback = std::function<void(const std::vector<OutputLine>& lines)>;
    using DoneCallback   = std::function<void(int exit_code)>;

    bool run_command_line_async(const std::string& command_line,
                                OutputCallback on_output,
                                DoneCallback on_done);

    struct SyncResult {
        int exit_code = -1;
        std::string stdout_output;
        std::string stderr_output;
    };

    static SyncResult run_sync(const std::string& executable,
                               const std::vector<std::string>& args);

    void write_stdin(const std::string& data);

    bool is_running() const;

    void terminate();

private:
    void on_timer(wxTimerEvent& event);
    void finish_process(int exit_code);
    void start_reader_threads();
    void join_reader_threads();
    void enqueue_output(std::string line, bool is_stderr);
    std::vector<OutputLine> drain_output_queue();
    void join_launch_thread();

    wxTimer m_poll_timer;

    std::atomic_bool m_launching{false};
    std::atomic_bool m_launch_failed{false};
    std::thread      m_launch_thread;
    std::thread      m_stdout_thread;
    std::thread      m_stderr_thread;
    std::mutex       m_output_mutex;
    std::vector<OutputLine> m_output_queue;

    std::unique_ptr<boost::process::child> m_process;
    std::unique_ptr<boost::process::ipstream> m_stdout_pipe;
    std::unique_ptr<boost::process::ipstream> m_stderr_pipe;
    std::unique_ptr<boost::process::opstream> m_stdin_pipe;

    OutputCallback m_on_output;
    DoneCallback m_on_done;
};

}}

#endif
