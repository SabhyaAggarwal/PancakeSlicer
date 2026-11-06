#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {
class Print;
class PrintObject;
struct GCodeProcessorResult;
namespace Nonplanar {

struct CurvedTopBudgetPatch {
    int id = -1;
    double planar_s = 0., curved_s = 0., cost_s = 0.;
    double planar_rms_mm = 0., curved_rms_mm = 0., rms_gain_mm = 0.;
    size_t samples = 0;
    bool dropped = false;
    std::string reason;
};
struct CurvedTopBudgetReport {
    bool measured = false, within_budget = false;
    double planar_s = 0., candidate_s = 0., limit_s = 0.;
    double time_budget_percent = 0.;
    std::vector<CurvedTopBudgetPatch> patches;
    std::string reason;
};



class CurvedTopsBudget {
public:
    CurvedTopsBudget();
    ~CurvedTopsBudget();


    static constexpr int Walls = -2;


    static constexpr int Fallback = -3;

    static constexpr int NativeRoads = -4;
    std::vector<std::pair<PrintObject *, int>> reject(Print &print, const GCodeProcessorResult &result);
    CurvedTopBudgetReport report(const PrintObject &object) const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
}
