#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace Slic3r { namespace GUI {

class StagedBuild
{
public:
    virtual ~StagedBuild() = default;

    bool built() const
    {
        return m_next_step == m_steps.size() &&
               std::all_of(m_children.begin(), m_children.end(), [](const StagedBuild* child) { return child->built(); });
    }

    bool build_step()
    {
        if (m_next_step < m_steps.size()) {
            auto step = std::move(m_steps[m_next_step++]);
            step();
        } else if (StagedBuild* child = unfinished_child()) {
            child->build_step();
        }
        return !built();
    }

protected:
    void add_build_step(std::function<void()> step) { m_steps.push_back(std::move(step)); }

    void add_build_steps_of(StagedBuild& child)
    {
        m_children.push_back(&child);
        for (size_t i = child.m_next_step; i < child.m_steps.size(); ++i)
            add_build_step([&child] { child.build_step(); });
    }

private:
    StagedBuild* unfinished_child() const
    {
        auto it = std::find_if(m_children.begin(), m_children.end(), [](const StagedBuild* child) { return !child->built(); });
        return it == m_children.end() ? nullptr : *it;
    }

    std::vector<std::function<void()>> m_steps;
    std::vector<StagedBuild*>          m_children;
    size_t                             m_next_step{ 0 };
};

}}
