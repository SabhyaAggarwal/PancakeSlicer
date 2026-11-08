#include "BeltGCode.hpp"
#include "GCodeWriter.hpp"
#include "GCode/BeltKinematics.hpp"
#include "BeltTransform.hpp"
#include "Print.hpp"
#include "Point.hpp"
#include "PrintConfig.hpp"
#include "libslic3r.h"
#include <cstdlib>

namespace Slic3r {

void BeltGCode::init_belt_writer(Print &print)
{
    install_belt_kinematics(m_writer, print.config());
    m_writer.set_force_normal_lift(true);
}

void BeltGCode::write_belt_header(GCodeOutputStream &file, const Print &print)
{
    const auto &full_cfg = print.full_print_config();
    file.write_format("; belt_slice_rotation = %s\n", full_cfg.opt_serialize("belt_slice_rotation").c_str());
    file.write_format("; belt_slice_rotation_angle = %.1f\n", print.config().belt_slice_rotation_angle.value);
    file.write_format("; belt_frame_tilt_decouple = %d\n", print.config().belt_frame_tilt_decouple.value ? 1 : 0);
    file.write_format("; belt_frame_tilt_angle = %.1f\n", print.config().belt_frame_tilt_angle.value);
}

void BeltGCode::on_set_origin(const PrintObject * , const Point & )
{
    Transform3d T = BeltTransformPipeline::build_forward_transform(m_config);
    Vec2d cur_origin = this->origin();
    Vec3d origin3d(cur_origin.x(), cur_origin.y(), 0.);
    Vec3d adjusted = T.linear() * origin3d;
    this->set_origin(Vec2d(adjusted.x(), adjusted.y()));
}

}
