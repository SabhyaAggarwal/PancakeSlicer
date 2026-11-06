#pragma once

#include <chrono>
#include <mutex>
#include <string>

namespace Slic3r {





















class InstanceLock
{
public:


    static constexpr std::chrono::milliseconds default_timeout{1000};


    static inline std::chrono::milliseconds cooldown{10000};


    explicit InstanceLock(const std::string &lock_file_path, std::chrono::milliseconds timeout = default_timeout);
    ~InstanceLock();

    InstanceLock(const InstanceLock &) = delete;
    InstanceLock &operator=(const InstanceLock &) = delete;


    bool locked() const { return m_locked; }

private:
    struct Slot;
    static Slot &slot_for(const std::string &lock_file_path);
    static bool  open_lock_file(Slot &slot, const std::string &lock_file_path);
    static void  defer(Slot &slot, const std::string &reason);

    Slot *m_slot{nullptr};
    std::unique_lock<std::recursive_mutex> m_slot_guard;
    bool  m_locked{false};
};

}
