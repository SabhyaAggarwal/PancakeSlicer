#ifndef slic3r_ThreadStandards_hpp_
#define slic3r_ThreadStandards_hpp_

#include <string>
#include <vector>

namespace Slic3r {







struct ThreadSpec {
    enum class Series { MetricCoarse, MetricFine, UNC, UNF };

    std::string name;
    double      major_diameter_mm;
    double      pitch_mm;
    Series      series;


    double thread_depth_mm() const { return 0.6134 * pitch_mm; }

    double minor_diameter_mm() const { return major_diameter_mm - 1.0825 * pitch_mm; }

    double internal_depth_mm() const { return 0.5 * (major_diameter_mm - minor_diameter_mm()); }

    bool imperial() const { return series == Series::UNC || series == Series::UNF; }
};


const std::vector<ThreadSpec>& thread_standards();


const ThreadSpec* find_thread_standard(const std::string& name);

}

#endif
