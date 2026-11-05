#pragma once
#include "libslic3r/Nonplanar/Repair.hpp"
#include <cstddef>
#include <map>
#include <memory>
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
class CurvedTopsBudget;
struct CurvedTopBudgetReport;

class ClearanceRepair
{
public:
    explicit ClearanceRepair(Print &print);
    ~ClearanceRepair();
    ClearanceRepair(const ClearanceRepair &) = delete;
    ClearanceRepair &operator=(const ClearanceRepair &) = delete;

    static void check_export(Print &print, GCodeProcessorResult &result);

    static CurvedTopBudgetReport curved_budget_report(const Print &print, const PrintObject &object);

    static size_t curved_contacts(const Print &print, const GCodeProcessorResult &result);

    bool apply();

    bool plan(const GCodeProcessorResult &result, const ClearanceReport &report, const HeadEnvelope &head, bool bar = false);
    bool plan_upright(const GCodeProcessorResult &result, const ClearanceReport &report);

private:
    bool oneshot_step(GCodeProcessorResult &result);
    bool curved_tops_step(GCodeProcessorResult &result);
    int m_curved_pass = 0;
    std::unique_ptr<CurvedTopsBudget> m_curved_budget;
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
    std::map<const PrintObject *, double> m_first_floating;
    size_t                   m_best_hits    = 0;
    int                      m_stale_passes = 0;
    std::vector<Planned>     m_planned;
    std::vector<PrintObject *> m_repick;
    std::map<const PrintObject *, int> m_repicks;
    std::map<const PrintObject *, bool> m_band_tried;
    int                      m_guard_passes = 0;
    std::vector<std::string> m_guard_warnings;
    std::vector<std::string> m_actions;
};

}
}
