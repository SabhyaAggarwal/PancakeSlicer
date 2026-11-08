#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Slic3r::GUI { class StagedBuild; }


class wxBusyCursor;

namespace Slic3r { namespace GUI {

template <class Self> class LazyInstance;

class LazyBase
{
public:
    virtual ~LazyBase() = default;

    virtual const std::string& name() const = 0;
    virtual bool               built() const = 0;
    virtual bool pending() const { return !built(); }
    virtual bool build_step() = 0;
    virtual int prebuild_order() const = 0;

protected:
    class OnDemandBuild
    {
    public:
        explicit OnDemandBuild(const LazyBase& lazy);
        ~OnDemandBuild();
        void unit() { ++m_units; }

    private:
        const LazyBase&               m_lazy;
        std::unique_ptr<wxBusyCursor> m_busy;
        std::chrono::steady_clock::time_point m_started;
        int                           m_units{ 0 };
    };

    static void log_null_factory(const std::string& name);
};

template <class T>
class Lazy : public LazyBase
{
public:
    using Factory = std::function<T*()>;

    Lazy(std::string name, int order, Factory make) : m_name(std::move(name)), m_order(order), m_make(std::move(make))
    {
        if constexpr (registered())
            LazyInstance<T>::s_lazy = this;
    }
    ~Lazy() override
    {
        if constexpr (registered())
            if (LazyInstance<T>::s_lazy == this)
                LazyInstance<T>::s_lazy = nullptr;
    }
    Lazy(const Lazy&) = delete;
    Lazy& operator=(const Lazy&) = delete;

    T* get() const { return built() ? m_object : nullptr; }

    T* ensure()
    {
        if (!built() && !m_building) {
            OnDemandBuild build(*this);
            do
                build.unit();
            while (build_step());
        }
        return get();
    }

    void when_built(std::function<void(T&)> fn)
    {
        if (built())
            fn(*m_object);
        else
            m_deferred.push_back(std::move(fn));
    }

    const std::string& name() const override { return m_name; }
    bool built() const override { return m_complete.load(std::memory_order_acquire); }
    bool pending() const override { return !built() && !m_failed; }
    int  prebuild_order() const override { return m_order; }

    bool build_step() override
    {
        if (built() || m_building || m_failed)
            return false;
        m_building = true;
        try {
            if (m_object == nullptr)
                m_object = m_make();
            else
                step();
        } catch (...) {
            m_building = false;
            throw;
        }
        m_building = false;
        if (m_object == nullptr) {
            m_failed = true;
            log_null_factory(m_name);
            return false;
        }
        if (steps_remain())
            return true;
        m_complete.store(true, std::memory_order_release);
        for (auto& fn : m_deferred)
            fn(*m_object);
        m_deferred.clear();
        return false;
    }

private:
    static constexpr bool registered() { return std::is_base_of_v<LazyInstance<T>, T>; }
    static constexpr bool staged() { return std::is_base_of_v<StagedBuild, T>; }

    bool steps_remain() const
    {
        if constexpr (staged())
            return !m_object->built();
        else
            return false;
    }
    void step()
    {
        if constexpr (staged())
            m_object->build_step();
    }

    std::string                          m_name;
    int                                  m_order;
    Factory                              m_make;
    T*                                   m_object{ nullptr };
    std::atomic<bool>                    m_complete{ false };
    bool                                 m_building{ false };
    bool                                 m_failed{ false };
    std::vector<std::function<void(T&)>> m_deferred;
};

template <class Self>
class LazyInstance
{
public:
    static Self* if_built() { return s_lazy ? s_lazy->get() : nullptr; }
    static Self* ensure() { return s_lazy ? s_lazy->ensure() : nullptr; }
    static void when_built(std::function<void(Self&)> fn)
    {
        if (s_lazy)
            s_lazy->when_built(std::move(fn));
    }

private:
    friend class Lazy<Self>;
    inline static Lazy<Self>* s_lazy{ nullptr };
};

}}
