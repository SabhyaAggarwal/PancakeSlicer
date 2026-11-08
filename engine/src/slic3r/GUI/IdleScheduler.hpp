#pragma once

#include <functional>
#include <string>

#include <wx/event.h>
#include <wx/timer.h>

#include "PrebuildQueue.hpp"

namespace Slic3r::GUI { class LazyBase; }

namespace Slic3r { namespace GUI {

class IdleScheduler
{
public:
    explicit IdleScheduler(std::function<int()> input_idle_ms);

    void add(LazyBase& task) { m_queue.add(task); }
    void clear() { m_queue.clear(); }
    std::string names() const { return m_queue.names(); }

    void start();
    void stop();

private:
    void tick();

    PrebuildQueue        m_queue;
    wxTimer              m_timer;
    std::function<int()> m_input_idle_ms;
    bool                 m_in_slice{ false };
};

}}
