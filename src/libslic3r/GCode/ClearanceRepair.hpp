#pragma once
#include "libslic3r/Nonplanar/Repair.hpp"
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {
class Print;
class PrintObject;
struct GCodeProcessorResult;
struct ClearanceReport;

namespace Nonplanar {

struct HeadEnvelope;

class ClearanceRepair
{
public:
    explicit ClearanceRepair(Print &print);
    ~ClearanceRepair();
    ClearanceRepair(const ClearanceRepair &) = delete;
    ClearanceRepair &operator=(const ClearanceRepair &) = delete;

    static void check_export(Print &print, GCodeProcessorResult &result);

    bool apply();

    bool plan(const GCodeProcessorResult &result, const ClearanceReport &report, const HeadEnvelope &head, bool bar = false);
    bool plan_upright(const GCodeProcessorResult &result, const ClearanceReport &report);

private:
    bool oneshot_step(GCodeProcessorResult &result);
    bool contact_guard(const GCodeProcessorResult &result, size_t contacts = 0);

    struct Planned
    {
        PrintObject *object = nullptr;
        RepairHints  hints;
    };
    std::string summary(size_t hits_now) const;

    Print                   &m_print;
    int                      m_max_passes = 0;
    int                      m_pass       = 0;
    size_t                   m_first_hits = 0;
    size_t                   m_last_hits  = 0;
    double                   m_last_min   = 0.;
    std::vector<Planned>     m_planned;
    std::vector<PrintObject *> m_repick;
    int                      m_guard_passes = 0;
    std::vector<std::string> m_guard_warnings;
    std::vector<std::string> m_actions;
};

}
}
