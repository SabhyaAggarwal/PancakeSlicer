#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "Lazy.hpp"

namespace Slic3r { namespace GUI {

class PrebuildQueue
{
public:
    struct Slice
    {
        std::string name;
        int         units{ 0 };
        long long   ms{ 0 };
        bool        completed{ false };
        bool        remaining{ false };
    };

    void add(LazyBase& task)
    {
        auto after = std::find_if(m_tasks.begin(), m_tasks.end(), [&](const LazyBase* t) { return t->prebuild_order() > task.prebuild_order(); });
        m_tasks.insert(after, &task);
    }

    void clear() { m_tasks.clear(); }

    bool pending() const
    {
        return std::any_of(m_tasks.begin(), m_tasks.end(), [](const LazyBase* t) { return t->pending(); });
    }

    std::string names() const
    {
        std::string out;
        for (const LazyBase* t : m_tasks)
            out += (out.empty() ? "" : ", ") + t->name();
        return out;
    }

    Slice run_slice(int                                                       budget_ms,
                    const std::function<long long()>&                         now_ms,
                    const std::function<bool()>&                              interrupt,
                    const std::function<void(const std::string&, long long)>& on_unit = {})
    {
        Slice slice;
        auto  next = std::find_if(m_tasks.begin(), m_tasks.end(), [](const LazyBase* t) { return t->pending(); });
        if (next == m_tasks.end())
            return slice;
        LazyBase* const task    = *next;
        slice.name              = task->name();
        const long long started = now_ms();
        for (;;) {
            const long long unit_started = now_ms();
            const bool      more         = task->build_step();
            const long long now          = now_ms();
            ++slice.units;
            if (on_unit)
                on_unit(slice.name, now - unit_started);
            slice.ms = now - started;
            if (!more) {
                slice.completed = true;
                break;
            }
            if (slice.ms >= budget_ms || interrupt())
                break;
        }
        slice.remaining = pending();
        return slice;
    }

private:
    std::vector<LazyBase*> m_tasks;
};

}}
