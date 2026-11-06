#pragma once



#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <optional>
#include <string>
#include <vector>

namespace Slic3r {
class Model;
struct GCodeProcessorResult;
struct ClearanceReport;
class ToolKinematics;
}

namespace Slic3r::Nonplanar::Calibration {

enum class Test
{
    Clearance,
    TiltFlow,
    CurvedTop,
    Verifier,
};


std::string         test_id(Test test);
std::optional<Test> parse_test(const std::string &id);

struct PlateObject
{
    std::string        name;
    TriangleMesh       mesh;
    Vec2d              center;
    DynamicPrintConfig config;
};

struct Plate
{
    std::vector<PlateObject> objects;
    DynamicPrintConfig       config;
};




Plate make_plate(Test test, const DynamicPrintConfig &config, const HeadEnvelope &head);

void add_to_model(const Plate &plate, Model &model);





struct ClearanceStep
{
    double slope_deg;
    double relief;
};
const std::vector<ClearanceStep> &clearance_steps();



constexpr const char *ProbeHeadId = "calibration-probe";


HeadEnvelope calibrated_head(const DynamicPrintConfig &edited, const DynamicPrintConfig &saved);




double step_clearance(const HeadEnvelope &head, const ClearanceStep &step, double gap, std::string *component = nullptr);

int predicted_first_touch(const HeadEnvelope &head, double gap);

struct ClearanceFit
{
    HeadEnvelope head;
    double       shift = 0.;
    double       slope_cap_deg = 0.;
    double       band_height = 0.;
    double       gantry_floor = 0.;
};




ClearanceFit fit_clearance(const HeadEnvelope &base, int last_clean, double gap, const std::string &id, const std::string &name);




int verifier_first_touch(const Plate &plate, const GCodeProcessorResult &result, const HeadEnvelope &head,
                         const ToolKinematics &kinematics, ClearanceReport *report = nullptr);




const std::vector<double> &tilt_slopes();
const std::vector<double> &tilt_multipliers();


std::vector<double> tilt_flow_result(const std::vector<double> &current, const std::vector<int> &picks);




const std::vector<double> &top_speed_factors();
const std::vector<double> &top_fan_boosts();

DynamicPrintConfig curved_top_result(int speed, int fan);

}
