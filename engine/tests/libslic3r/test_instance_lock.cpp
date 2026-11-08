#include <boost/filesystem/operations.hpp>
#include <catch2/catch_all.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#include <boost/filesystem.hpp>

#include <catch2/catch_test_macros.hpp>
#include "libslic3r/InstanceLock.hpp"
#include "test_utils.hpp"
#include <ios>

#ifdef _WIN32
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/nowide/fstream.hpp>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

using namespace Slic3r;
using namespace std::chrono_literals;

template<typename T> struct ScopedStaticValue
{
    T &ref;
    T  saved;
    ScopedStaticValue(T &ref, T value) : ref(ref), saved(ref) { ref = value; }
    ~ScopedStaticValue() { ref = saved; }
};

TEST_CASE("InstanceLock creates its lock file and holds it for the guard's scope", "[InstanceLock]")
{
    ScopedTemporaryFile lock_file(".lock");
    const std::string   path = lock_file.string();

    {
        InstanceLock lock(path);
        REQUIRE(lock.locked());
        REQUIRE(boost::filesystem::exists(path));
    }
    const auto   started = std::chrono::steady_clock::now();
    InstanceLock again(path, 5000ms);
    REQUIRE(again.locked());
    REQUIRE(std::chrono::steady_clock::now() - started < 4000ms);
}

TEST_CASE("InstanceLock nests within one thread", "[InstanceLock]")
{
    ScopedTemporaryFile lock_file(".lock");
    const std::string   path = lock_file.string();

    InstanceLock outer(path);
    {
        InstanceLock inner(path, 100ms);
        REQUIRE(inner.locked());
    }
    REQUIRE(outer.locked());
}

TEST_CASE("InstanceLock is a no-op for an empty path and survives an unwritable one", "[InstanceLock]")
{
    ScopedTemporaryDir dir;

    InstanceLock none("");
    REQUIRE_FALSE(none.locked());

    InstanceLock unwritable((dir.path() / "missing" / "shared.lock").string(), 100ms);
    REQUIRE_FALSE(unwritable.locked());
}

TEST_CASE("InstanceLock retries a lock file it could not open once the cool-down passes", "[InstanceLock]")
{
    ScopedTemporaryDir dir;
    const std::string  path = (dir.path() / "later" / "shared.lock").string();
    ScopedStaticValue  cooldown(InstanceLock::cooldown, 300ms);

    bool before_dir, during_cooldown, after_cooldown;
    const auto started = std::chrono::steady_clock::now();
    {
        InstanceLock lock(path, 100ms);
        before_dir = lock.locked();
    }
    boost::filesystem::create_directories(dir.path() / "later");
    {
        InstanceLock lock(path, 100ms);
        during_cooldown = lock.locked();
    }
    const bool second_guard_inside_cooldown = std::chrono::steady_clock::now() - started < InstanceLock::cooldown;
    std::this_thread::sleep_for(400ms);
    {
        InstanceLock lock(path, 100ms);
        after_cooldown = lock.locked();
    }

    REQUIRE_FALSE(before_dir);
    if (second_guard_inside_cooldown)
        REQUIRE_FALSE(during_cooldown);
    REQUIRE(after_cooldown);
}

TEST_CASE("InstanceLock reopens a lock file that was replaced on disk", "[InstanceLock]")
{
    ScopedTemporaryFile lock_file(".lock");
    const std::string   path = lock_file.string();
    {
        InstanceLock lock(path);
        REQUIRE(lock.locked());
    }

    boost::filesystem::remove(path);
    InstanceLock lock(path);
    REQUIRE(lock.locked());
    REQUIRE(boost::filesystem::exists(path));
}

TEST_CASE("InstanceLock serialises the threads of one process", "[InstanceLock]")
{
    ScopedTemporaryFile lock_file(".lock");
    const std::string   path = lock_file.string();

    std::atomic<bool> holder_ready{false};
    std::atomic<bool> holder_released{false};
    std::thread holder([&] {
        InstanceLock lock(path);
        holder_ready = true;
        std::this_thread::sleep_for(150ms);
        holder_released = true;
    });
    while (! holder_ready)
        std::this_thread::yield();

    bool released_before_acquire = false;
    {
        InstanceLock lock(path);
        released_before_acquire = holder_released;
    }
    holder.join();
    REQUIRE(released_before_acquire);
}

class OtherHolder
{
public:
    explicit OtherHolder(const std::string &path)
    {
#ifdef _WIN32
        boost::nowide::ofstream(path, std::ios::app).close();
        m_lock = boost::interprocess::file_lock(boost::nowide::widen(path).c_str());
        m_held = m_lock.try_lock();
#else
        m_fd   = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
        m_held = m_fd >= 0 && ::flock(m_fd, LOCK_EX | LOCK_NB) == 0;
#endif
    }
    ~OtherHolder() { release(); }
    OtherHolder(const OtherHolder &) = delete;
    OtherHolder &operator=(const OtherHolder &) = delete;

    bool held() const { return m_held; }
    void release()
    {
#ifdef _WIN32
        if (m_held)
            m_lock.unlock();
        m_lock = boost::interprocess::file_lock();
#else
        if (m_fd >= 0)
            ::close(m_fd);
        m_fd = -1;
#endif
        m_held = false;
    }

private:
#ifdef _WIN32
    boost::interprocess::file_lock m_lock;
#else
    int m_fd{-1};
#endif
    bool m_held{false};
};

TEST_CASE("InstanceLock yields to a lock held through another handle and reports it", "[InstanceLock]")
{
    ScopedTemporaryFile lock_file(".lock");
    const std::string   path = lock_file.string();
    ScopedStaticValue   cooldown(InstanceLock::cooldown, 300ms);

    OtherHolder other(path);
    REQUIRE(other.held());

    bool locked_while_other_holds;
    {
        InstanceLock lock(path, 100ms);
        locked_while_other_holds = lock.locked();
    }
    const auto started = std::chrono::steady_clock::now();
    bool       locked_during_cooldown;
    {
        InstanceLock lock(path, 5000ms);
        locked_during_cooldown = lock.locked();
    }
    const auto cooldown_wait = std::chrono::steady_clock::now() - started;
    other.release();

    REQUIRE_FALSE(locked_while_other_holds);
    REQUIRE_FALSE(locked_during_cooldown);
    REQUIRE(cooldown_wait < 4000ms);
    std::this_thread::sleep_for(400ms);
    InstanceLock lock(path);
    REQUIRE(lock.locked());
}
