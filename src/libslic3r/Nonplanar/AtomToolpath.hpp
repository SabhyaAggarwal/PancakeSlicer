#pragma once
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Point.hpp"

#include <cstddef>
#include <istream>
#include <string>
#include <vector>

namespace Slic3r { class PrintObject; }

namespace Slic3r::Nonplanar {

struct ConformalResult;

struct AtomPoint
{
    Vec3d  tip { Vec3d::Zero() };
    Vec3d  axis { Vec3d::UnitZ() };
    bool   deposit = false;
    double width  = 0.;
    double height = 0.;
};

struct AtomToolpath
{
    std::vector<AtomPoint> points;
    double platform_height = 0.;
    size_t deposition_edges() const;
    double deposition_length() const;
    double travel_length() const;
    double max_tilt() const;
    BoundingBoxf3 deposition_bbox() const;
};

AtomToolpath read_atom_poses(std::istream &in, double width, double height);
AtomToolpath load_atom_pathdata(const std::string &npz_path);
AtomToolpath load_atom_toolpath(const std::string &path, double width, double height);

struct AtomPlacement
{
    Matrix3d rotation { Matrix3d::Identity() };
    Vec3d    offset { Vec3d::Zero() };
};

ExtrusionEntityCollection atom_conformal_paths(const AtomToolpath &toolpath, const AtomPlacement &placement,
                                               double host_z, ExtrusionRole role, bool keep_tool_axis,
                                               int patch_id = -1, int shell = 0);

AtomPlacement atom_placement(const PrintObject &object, const AtomToolpath &toolpath);

std::vector<std::string> apply_atom_toolpath(PrintObject &object, ConformalResult &result);

}
