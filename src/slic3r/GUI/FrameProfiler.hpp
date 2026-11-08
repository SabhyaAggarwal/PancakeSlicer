#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <vector>

namespace Slic3r {
namespace GUI {

class FrameProfiler
{
public:
    struct Section
    {
        const char* name{ nullptr };
        double cpu_ms{ 0.0 };
        double gpu_ms{ 0.0 };
    };

    void begin_frame();
    void mark(const char* name);
    void end_frame();
    const std::vector<Section>& sections() const { return m_sections; }
    void start_averaging();
    bool is_averaging() const { return m_averaging; }
    std::vector<Section> finish_averaging(bool wait);
    void reset();

private:
    static constexpr size_t FRAMES_IN_FLIGHT = 4;
    static constexpr size_t MAX_SECTIONS = 32;

    struct Frame
    {
        std::array<unsigned int, 2 * MAX_SECTIONS> queries{};
        std::array<const char*, MAX_SECTIONS> names{};
        std::array<double, MAX_SECTIONS> cpu_ms{};
        size_t count{ 0 };
        bool pending{ false };
        bool averaged{ false };
    };

    void collect(bool wait);

    std::array<Frame, FRAMES_IN_FLIGHT> m_frames;
    Frame* m_recording{ nullptr };
    size_t m_next{ 0 };
    std::chrono::steady_clock::time_point m_last_mark;
    std::vector<Section> m_sections;
    bool m_averaging{ false };
    std::vector<Section> m_sums;
    size_t m_averaged_frames{ 0 };
};

}
}
