#include "AtomToolpath.hpp"

#include "BeadModel.hpp"
#include "ConformalShells.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/miniz_extension.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/format.hpp>
#include <boost/nowide/fstream.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <sstream>

namespace Slic3r::Nonplanar {

namespace {

[[noreturn]] void fail(const std::string &what) { throw RuntimeError("Atomizer toolpath: " + what); }

constexpr double UprightTolerance = 1e-3;

double tilt(const Vec3d &axis) { return std::acos(std::clamp(axis.z(), -1., 1.)); }

struct NpyArray
{
    std::vector<size_t> shape;
    std::vector<double> values;
    size_t              count() const
    {
        size_t n = 1;
        for (size_t s : shape) n *= s;
        return n;
    }
};

NpyArray parse_npy(const std::string &name, const unsigned char *data, size_t size)
{
    if (size < 10 || std::memcmp(data, "\x93NUMPY", 6) != 0)
        fail(name + " is not a .npy array");
    const int major = data[6];
    size_t    header_len, offset;
    if (major == 1) {
        header_len = size_t(data[8]) | (size_t(data[9]) << 8);
        offset     = 10;
    } else {
        if (size < 12) fail(name + " is truncated");
        header_len = size_t(data[8]) | (size_t(data[9]) << 8) | (size_t(data[10]) << 16) | (size_t(data[11]) << 24);
        offset     = 12;
    }
    if (offset + header_len > size)
        fail(name + " is truncated");
    const std::string header(reinterpret_cast<const char *>(data + offset), header_len);
    offset += header_len;

    auto value_of = [&](const std::string &key) {
        const size_t k = header.find("'" + key + "'");
        if (k == std::string::npos) fail(name + " has no '" + key + "'");
        const size_t colon = header.find(':', k);
        return header.substr(colon + 1);
    };
    std::string descr = value_of("descr");
    descr             = descr.substr(descr.find('\'') + 1);
    descr             = descr.substr(0, descr.find('\''));
    if (value_of("fortran_order").find("True") < value_of("fortran_order").find(','))
        fail(name + " is in Fortran order");
    std::string shape = value_of("shape");
    shape             = shape.substr(shape.find('(') + 1, shape.find(')') - shape.find('(') - 1);

    NpyArray out;
    std::istringstream dims(shape);
    for (std::string dim; std::getline(dims, dim, ',');)
        if (dim.find_first_not_of(" ") != std::string::npos)
            out.shape.push_back(size_t(std::stoull(dim)));

    if (descr.size() < 3 || descr[0] == '>')
        fail(name + " has unsupported dtype " + descr);
    const char   kind  = descr[1];
    const size_t bytes = size_t(std::stoul(descr.substr(2)));
    const size_t n     = out.count();
    if (offset + n * bytes > size)
        fail(name + " is truncated");
    out.values.resize(n);
    const unsigned char *p = data + offset;
    for (size_t i = 0; i < n; ++i, p += bytes) {
        double v;
        if (kind == 'f' && bytes == 8)      { double x; std::memcpy(&x, p, 8); v = x; }
        else if (kind == 'f' && bytes == 4) { float x; std::memcpy(&x, p, 4); v = x; }
        else if (kind == 'i' && bytes == 8) { int64_t x; std::memcpy(&x, p, 8); v = double(x); }
        else if (kind == 'i' && bytes == 4) { int32_t x; std::memcpy(&x, p, 4); v = x; }
        else if (kind == 'i' && bytes == 2) { int16_t x; std::memcpy(&x, p, 2); v = x; }
        else if (kind == 'i' && bytes == 1) { v = double(int8_t(*p)); }
        else if ((kind == 'u' || kind == 'b') && bytes == 1) { v = *p; }
        else if (kind == 'u' && bytes == 8) { uint64_t x; std::memcpy(&x, p, 8); v = double(x); }
        else fail(name + " has unsupported dtype " + descr);
        out.values[i] = v;
    }
    return out;
}

}

size_t AtomToolpath::deposition_edges() const
{
    size_t n = 0;
    for (size_t i = 1; i < points.size(); ++i) n += points[i].deposit;
    return n;
}

double AtomToolpath::deposition_length() const
{
    double l = 0.;
    for (size_t i = 1; i < points.size(); ++i)
        if (points[i].deposit) l += (points[i].tip - points[i - 1].tip).norm();
    return l;
}

double AtomToolpath::travel_length() const
{
    double l = 0.;
    for (size_t i = 1; i < points.size(); ++i)
        if (! points[i].deposit) l += (points[i].tip - points[i - 1].tip).norm();
    return l;
}

double AtomToolpath::max_tilt() const
{
    double t = 0.;
    for (size_t i = 1; i < points.size(); ++i)
        if (points[i].deposit) t = std::max({ t, tilt(points[i - 1].axis), tilt(points[i].axis) });
    return t;
}

BoundingBoxf3 AtomToolpath::deposition_bbox() const
{
    BoundingBoxf3 bb;
    for (size_t i = 1; i < points.size(); ++i)
        if (points[i].deposit) {
            bb.merge(points[i - 1].tip);
            bb.merge(points[i].tip);
        }
    return bb;
}

AtomToolpath read_atom_poses(std::istream &in, double width, double height)
{
    if (! (width > 0.) || ! (height > 0.))
        fail("the native format carries no bead size; width and height must be positive");
    AtomToolpath out;
    std::string  line;
    long long    count = -1;
    if (std::getline(in, line)) {
        std::istringstream ls(line);
        ls >> count;
    }
    if (count < 2)
        fail("line 1 must hold a point count of at least 2");
    out.points.reserve(size_t(count));
    size_t line_no = 1;
    while (std::getline(in, line)) {
        ++line_no;
        if (line.find_first_not_of(" \t\r") == std::string::npos)
            continue;
        std::istringstream ls(line);
        std::string        flag;
        AtomPoint          p;
        ls >> flag >> p.tip.x() >> p.tip.y() >> p.tip.z() >> p.axis.x() >> p.axis.y() >> p.axis.z();
        if (ls.fail() || (flag != "D" && flag != "T"))
            fail("line " + std::to_string(line_no) + " is not 'D|T x y z nx ny nz'");
        if (! p.tip.allFinite() || ! p.axis.allFinite() || std::abs(p.axis.norm() - 1.) > 1e-5)
            fail("line " + std::to_string(line_no) + " has a nonfinite point or a non-unit tool axis");
        p.axis.normalize();
        p.deposit = flag == "D";
        p.width   = width;
        p.height  = height;
        out.points.push_back(p);
    }
    if (out.points.size() != size_t(count))
        fail("the header announces " + std::to_string(count) + " points, the file holds " + std::to_string(out.points.size()));
    return out;
}

AtomToolpath load_atom_pathdata(const std::string &npz_path)
{
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (! open_zip_reader(&zip, npz_path))
        fail("cannot open " + npz_path);
    struct Closer { mz_zip_archive *z; ~Closer() { close_zip_reader(z); } } closer { &zip };

    auto read = [&zip](const std::string &name, bool required) {
        size_t size = 0;
        void  *data = mz_zip_reader_extract_file_to_heap(&zip, (name + ".npy").c_str(), &size, 0);
        if (data == nullptr) {
            if (required) fail("missing array '" + name + "'");
            return NpyArray();
        }
        std::unique_ptr<void, decltype(&mz_free)> owner(data, &mz_free);
        return parse_npy(name, static_cast<const unsigned char *>(data), size);
    };
    const NpyArray point = read("point", true), orientation = read("tool_orientation", true),
                   travel = read("travel_type", true), width = read("width", true), height = read("height", true),
                   point_count = read("point_count", true), platform = read("platform_height", false);

    if (point_count.values.size() != 1)
        fail("point_count must be a scalar");
    const size_t n = size_t(point_count.values.front());
    auto check = [n](const NpyArray &a, const std::string &name, size_t cols) {
        if (a.shape.empty() || a.shape[0] < n || (cols > 1 ? a.shape.size() != 2 || a.shape[1] != cols : a.shape.size() != 1))
            fail("array '" + name + "' does not hold point_count rows of " + std::to_string(cols));
    };
    if (n < 2)
        fail("point_count must be at least 2");
    check(point, "point", 3);
    check(orientation, "tool_orientation", 2);
    check(travel, "travel_type", 1);
    check(width, "width", 1);
    check(height, "height", 1);

    AtomToolpath out;
    out.platform_height = platform.values.empty() ? 0. : platform.values.front();
    out.points.resize(n);
    for (size_t i = 0; i < n; ++i) {
        AtomPoint   &p     = out.points[i];
        const double theta = orientation.values[2 * i], phi = orientation.values[2 * i + 1];
        p.tip     = Vec3d(point.values[3 * i], point.values[3 * i + 1], point.values[3 * i + 2]);
        p.axis    = Vec3d(std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta));
        p.width   = width.values[i];
        p.height  = height.values[i];
        const double t = travel.values[i];
        if (t != 0. && t != 1.)
            fail("travel_type must be 0 (deposition) or 1 (travel), row " + std::to_string(i));
        p.deposit = t == 0. && i > 0;
        if (! p.tip.allFinite() || ! p.axis.allFinite() || ! std::isfinite(p.width) || ! std::isfinite(p.height) ||
            p.width < 0. || p.height < 0. || (p.deposit && (p.width <= 0. || p.height <= 0.)))
            fail("row " + std::to_string(i) + " has a nonfinite value or a nonpositive bead");
    }
    return out;
}

AtomToolpath load_atom_toolpath(const std::string &path, double width, double height)
{
    if (boost::iends_with(path, ".npz"))
        return load_atom_pathdata(path);
    boost::nowide::ifstream in(path);
    if (! in)
        fail("cannot open " + path);
    return read_atom_poses(in, width, height);
}

ExtrusionEntityCollection atom_conformal_paths(const AtomToolpath &toolpath, const AtomPlacement &placement,
                                               double host_z, ExtrusionRole role, bool keep_tool_axis,
                                               int patch_id, int shell)
{
    ExtrusionEntityCollection out;
    out.no_sort = true;
    const std::vector<AtomPoint> &pts = toolpath.points;

    Points3            poly;
    std::vector<float> heights;
    std::vector<Vec3f> axes;
    double             width = 0.;
    auto add_point = [&](const AtomPoint &p, double height) {
        const Vec3d   tip  = placement.rotation * p.tip + placement.offset;
        const Vec3d   axis = (placement.rotation * p.axis).normalized();
        const Point3  q(scale_(tip.x()), scale_(tip.y()), scale_(tip.z() - host_z));
        if (! poly.empty() && poly.back() == q && (axes.empty() || (axes.back().cast<double>() - axis).norm() < 1e-9))
            return;
        if (! keep_tool_axis && tilt(axis) > UprightTolerance)
            fail((boost::format("the tool tilts by %1$.1f degrees, but this printer has no rotary axes. "
                                "Run AtomSlicer with -U -t 0 -b 0 for a three-axis printer.") % Geometry::rad2deg(tilt(axis))).str());
        poly.push_back(q);
        heights.push_back(float(height));
        if (keep_tool_axis)
            axes.push_back(axis.cast<float>());
    };
    auto flush = [&]() {
        if (poly.size() >= 2) {
            const double h = *std::max_element(heights.begin(), heights.end());
            ExtrusionPath tmpl(role, bead_area(width, h), float(width), float(h));
            auto *path     = new ExtrusionPathConformal(Polyline3(poly), tmpl, std::move(heights), std::move(axes));
            path->patch_id = patch_id;
            path->shell    = shell;
            out.entities.push_back(path);
        }
        poly.clear();
        heights.clear();
        axes.clear();
    };

    for (size_t i = 1; i < pts.size(); ++i) {
        if (! pts[i].deposit) {
            flush();
            continue;
        }
        if (! poly.empty() && std::abs(pts[i].width - width) > 1e-4)
            flush();
        if (poly.empty()) {
            width = pts[i].width;
            add_point(pts[i - 1], pts[i - 1].height > 0. ? pts[i - 1].height : pts[i].height);
        }
        add_point(pts[i], pts[i].height);
    }
    flush();
    return out;
}

AtomPlacement atom_placement(const PrintObject &object, const AtomToolpath &toolpath)
{
    const Transform3d &trafo  = object.instances().front().model_instance->get_matrix();
    const Matrix3d     linear = trafo.linear();
    if (! (linear.transpose() * linear).isIdentity(1e-6) || linear.determinant() < 0. ||
        (linear * Vec3d::UnitZ() - Vec3d::UnitZ()).norm() > 1e-6)
        fail("the object may only be rotated about Z; scaling, mirroring or tilting it would change the beads");

    AtomPlacement placement;
    placement.rotation = linear;
    const BoundingBoxf3 source = toolpath.deposition_bbox();
    if (! source.defined)
        fail("the toolpath deposits nothing");
    BoundingBoxf3 rotated;
    for (const Vec3d &c : { source.min, source.max, Vec3d(source.min.x(), source.max.y(), 0.), Vec3d(source.max.x(), source.min.y(), 0.) })
        rotated.merge(Vec3d(linear * c));
    double             ground = 0.;
    const BoundingBoxf3 mesh  = object.object_frame_mesh(ground).bounding_box();
    const Vec2d         shift = (mesh.min + mesh.max).head<2>() / 2. - (rotated.min + rotated.max).head<2>() / 2.;
    placement.offset          = Vec3d(shift.x(), shift.y(), object.slicing_parameters().object_print_z_min);
    return placement;
}

std::vector<std::string> apply_atom_toolpath(PrintObject &object, ConformalResult &result)
{
    std::vector<std::string> warnings;
    LayerPtrs               &layers = object.layers();
    if (layers.empty() || object.num_printing_regions() == 0)
        return warnings;
    const PrintRegion &region   = object.printing_region(0);
    const double       height   = object.config().layer_height.value;
    const double       width    = region.flow(object, frSolidInfill, height).width();
    const std::string &filename = object.config().nonplanar_atom_toolpath.value;
    const AtomToolpath toolpath = load_atom_toolpath(filename, width, height);
    const bool         rotary   = object.print()->config().nonplanar_kinematics.value != RotaryLayout::None;
    if (! rotary && toolpath.max_tilt() > UprightTolerance)
        fail((boost::format("%1% tilts the tool by up to %2$.1f degrees, but this printer has no rotary axes. "
                            "Run AtomSlicer with -U -t 0 -b 0 for a three-axis printer.") % filename % Geometry::rad2deg(toolpath.max_tilt())).str());

    const AtomPlacement placement = atom_placement(object, toolpath);
    {
        double              ground = 0.;
        const BoundingBoxf3 mesh   = object.object_frame_mesh(ground).bounding_box();
        const BoundingBoxf3 source = toolpath.deposition_bbox();
        const Vec3d         ms = mesh.size(), ts = source.size();
        const double        tol = std::max(1., 4. * width);
        if (std::abs(std::max(ms.x(), ms.y()) - std::max(ts.x(), ts.y())) > tol || std::abs(ms.z() - source.max.z()) > tol)
            warnings.push_back((boost::format("The Atomizer toolpath %1% spans %2$.1f x %3$.1f x %4$.1f mm, the object %5$.1f x %6$.1f x %7$.1f mm. "
                                              "Was it sliced from this model?") % filename % ts.x() % ts.y() % source.max.z() % ms.x() % ms.y() % ms.z()).str());
    }

    for (Layer *layer : layers) {
        for (LayerRegion *layerm : layer->regions()) {
            layerm->perimeters.clear();
            layerm->fills.clear();
            layerm->thin_fills.clear();
        }
        layer->conformal_patches_before.clear();
        layer->conformal_patches_after.clear();
        layer->conformal_covered = true;
    }

    const size_t   host = layers.size() - 1;
    ConformalPatch patch;
    patch.id           = 0;
    patch.kind         = PatchKind::Top;
    patch.shells       = 1;
    patch.shell_height = height;
    patch.host_layer   = host;
    patch.region_id    = 0;
    patch.shell_paths.push_back(atom_conformal_paths(toolpath, placement, layers[host]->print_z, erSolidInfill, rotary, patch.id, 0));
    const BoundingBoxf3 source = toolpath.deposition_bbox();
    patch.z_min = source.min.z() + placement.offset.z();
    patch.z_max = source.max.z() + placement.offset.z();

    result = ConformalResult();
    result.planar_exclusion.assign(layers.size(), ExPolygons());
    result.patches.push_back(std::move(patch));
    PatchDiagnostics diag;
    diag.patch_id      = 0;
    diag.max_slope_deg = Geometry::rad2deg(toolpath.max_tilt());
    result.diagnostics.push_back(diag);
    layers[host]->conformal_patches_after.push_back(0);
    return warnings;
}

}
