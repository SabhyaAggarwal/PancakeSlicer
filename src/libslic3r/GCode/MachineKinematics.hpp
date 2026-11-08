#ifndef slic3r_MachineKinematics_hpp_
#define slic3r_MachineKinematics_hpp_

#include "../Point.hpp"

namespace Slic3r {

class MachineKinematics
{
public:
    virtual ~MachineKinematics() = default;

    virtual Vec3d to_machine(const Vec3d &p) const = 0;

    virtual bool must_emit_all_axes() const = 0;

    virtual bool suppress_lift_at_unknown_position() const = 0;

    virtual bool supports_arc_moves() const = 0;

    virtual void set_axis_remap(int rx, int ry, int rz) = 0;
    virtual void set_build_volume_max(const Vec3d &max) = 0;
};

class CartesianKinematics : public MachineKinematics
{
public:
    Vec3d to_machine(const Vec3d &p) const override;

    bool must_emit_all_axes() const override { return this->has_axis_remap(); }
    bool suppress_lift_at_unknown_position() const override { return this->has_axis_remap(); }

    bool supports_arc_moves() const override { return m_remap_x == 0 && m_remap_y == 1; }

    void set_axis_remap(int rx, int ry, int rz) override
        { m_remap_x = rx; m_remap_y = ry; m_remap_z = rz; }
    void set_build_volume_max(const Vec3d &max) override { m_build_vol_max = max; }

    bool has_axis_remap() const
        { return m_remap_x != 0 || m_remap_y != 1 || m_remap_z != 2; }

protected:
    Vec3d apply_axis_remap(const Vec3d &pos) const;

    int   m_remap_x { 0 };
    int   m_remap_y { 1 };
    int   m_remap_z { 2 };
    Vec3d m_build_vol_max { Vec3d::Zero() };
};

}

#endif
