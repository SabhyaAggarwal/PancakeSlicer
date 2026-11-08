#ifndef slic3r_BeltKinematics_hpp_
#define slic3r_BeltKinematics_hpp_

#include "MachineKinematics.hpp"
#include "BeltBackTransform.hpp"
#include "MachineFrameTransform.hpp"
#include "../Point.hpp"

namespace Slic3r {

class PrintConfig;
class GCodeWriter;

class BeltKinematics : public CartesianKinematics
{
public:
    explicit BeltKinematics(const PrintConfig &config, bool world_coordinates = false);

    Vec3d to_machine(const Vec3d &p) const override;

    bool must_emit_all_axes() const override { return true; }
    bool suppress_lift_at_unknown_position() const override { return true; }
    bool supports_arc_moves() const override { return false; }

private:
    BeltBackTransform     m_back_transform;
    MachineFrameTransform m_machine_frame;
    bool                  m_world_coordinates { false };
};

void install_belt_kinematics(GCodeWriter &writer, const PrintConfig &config,
                             bool world_coordinates = false);

}

#endif
