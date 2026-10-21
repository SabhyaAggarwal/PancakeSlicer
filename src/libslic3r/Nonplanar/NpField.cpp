#include "NpField.hpp"

#include "BoundingBox.hpp"
#include "Npy.hpp"

#include "Point.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/miniz_extension.hpp"

#include <cstddef>
#include <array>
#include <miniz.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <optional>
#include <utility>

namespace Slic3r::Nonplanar {

namespace {

[[noreturn]] void fail(const std::string &path, const std::string &what) { throw RuntimeError("Layer field " + path + ": " + what); }

}

NpField load_npfield(const std::string &path)
{
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!open_zip_reader(&zip, path))
        fail(path, "cannot open it");
    struct Closer { mz_zip_archive *z; ~Closer() { close_zip_reader(z); } } closer { &zip };
    const std::string what = "Layer field " + path;
    auto read = [&](const std::string &name, bool required) { return read_npz_array(zip, name, required, what); };
    auto vec3 = [&](const std::string &name, bool required) -> std::optional<Vec3d> {
        const NpyArray a = read(name, required);
        if (a.values.empty() && !required)
            return std::nullopt;
        if (a.values.size() != 3 || !std::isfinite(a.values[0]) || !std::isfinite(a.values[1]) || !std::isfinite(a.values[2]))
            fail(path, "'" + name + "' must hold three finite numbers");
        return Vec3d(a.values[0], a.values[1], a.values[2]);
    };
    auto scalar = [&](const std::string &name, bool required, double fallback) {
        const NpyArray a = read(name, required);
        if (a.values.empty() && !required)
            return fallback;
        if (a.values.size() != 1 || !std::isfinite(a.values[0]))
            fail(path, "'" + name + "' must be a finite scalar");
        return a.values[0];
    };
    auto text = [&](const std::string &name) { return read(name, false).text; };

    NpField f;
    f.origin  = *vec3("origin", true);
    f.spacing = *vec3("spacing", true);
    if (f.spacing.minCoeff() <= 0.)
        fail(path, "'spacing' must be positive");
    const NpyArray dims = read("dims", true);
    if (dims.values.size() != 3)
        fail(path, "'dims' must hold three integers");
    for (int a = 0; a < 3; ++a) {
        const double v = dims.values[size_t(a)];
        if (!(v >= 2.) || std::floor(v) != v || v > 1e5)
            fail(path, "'dims' must be integers of at least 2");
        f.dims[size_t(a)] = size_t(v);
    }
    const NpyArray phi = read("phi", true);
    if (phi.shape.size() != 3 || phi.shape[0] != f.dims[0] || phi.shape[1] != f.dims[1] || phi.shape[2] != f.dims[2])
        fail(path, "'phi' must have the shape 'dims'");
    f.phi.resize(phi.values.size());
    for (size_t i = 0; i < phi.values.size(); ++i) {
        if (!std::isfinite(phi.values[i]))
            fail(path, "'phi' holds a nonfinite value");
        f.phi[i] = float(phi.values[i]);
    }
    f.h           = scalar("h", true, 0.);
    f.first_layer = scalar("first_layer", false, f.h);
    if (!(f.h > 0.) || !(f.first_layer > 0.))
        fail(path, "'h' and 'first_layer' must be positive");
    f.machine     = text("machine");
    f.mesh_sha256 = text("mesh_sha256");
    f.solver      = text("solver");
    f.proxy_version = text("proxy_version");
    f.mesh_min    = vec3("mesh_min", false);
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
        char name[256];
        mz_zip_reader_get_filename(&zip, i, name, sizeof(name));
        std::string n(name);
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".npy") == 0)
            n.resize(n.size() - 4);
        if (n == "J" || n.rfind("J_", 0) == 0 || n.rfind("stat_", 0) == 0)
            f.terms.emplace_back(n, scalar(n, true, 0.));
    }
    return f;
}

void save_npfield(const std::string &path, const NpField &f)
{
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!open_zip_writer(&zip, path))
        fail(path, "cannot write it");
    bool ok = true;
    auto add = [&](const std::string &name, const std::string &bytes) {
        ok = ok && mz_zip_writer_add_mem(&zip, (name + ".npy").c_str(), bytes.data(), bytes.size(), MZ_DEFAULT_COMPRESSION);
    };
    auto vec3 = [&](const std::string &name, const Vec3d &v) { add(name, npy_f8({ 3 }, v.data())); };
    vec3("origin", f.origin);
    vec3("spacing", f.spacing);
    const double dims[3] = { double(f.dims[0]), double(f.dims[1]), double(f.dims[2]) };
    add("dims", npy_f8({ 3 }, dims));
    add("phi", npy_f4({ f.dims[0], f.dims[1], f.dims[2] }, f.phi.data()));
    add("h", npy_f8({}, &f.h));
    add("first_layer", npy_f8({}, &f.first_layer));
    add("machine", npy_text(f.machine));
    add("mesh_sha256", npy_text(f.mesh_sha256));
    add("solver", npy_text(f.solver));
    if (!f.proxy_version.empty())
        add("proxy_version", npy_text(f.proxy_version));
    if (f.mesh_min)
        vec3("mesh_min", *f.mesh_min);
    for (const auto &[name, value] : f.terms)
        add(name, npy_f8({}, &value));
    ok = ok && mz_zip_writer_finalize_archive(&zip);
    close_zip_writer(&zip);
    if (!ok)
        fail(path, "writing it failed");
}

NpField planar_npfield(const BoundingBoxf3 &box, double pitch, double h, double first_layer)
{
    NpField f;
    f.h           = h;
    f.first_layer = first_layer;
    f.spacing     = Vec3d::Constant(pitch);
    f.origin      = Vec3d(box.min.x() - 2. * pitch, box.min.y() - 2. * pitch, 0.);
    for (int a = 0; a < 3; ++a)
        f.dims[size_t(a)] = size_t(std::ceil((box.max[a] + 2. * pitch - f.origin[a]) / pitch)) + 1;
    f.phi.resize(f.dims[0] * f.dims[1] * f.dims[2]);
    for (size_t i = 0; i < f.dims[0]; ++i)
        for (size_t j = 0; j < f.dims[1]; ++j)
            for (size_t k = 0; k < f.dims[2]; ++k)
                f.phi[f.index(i, j, k)] = float(f.node(i, j, k).z());
    f.mesh_min = box.min;
    f.solver   = "planar";
    return f;
}

void check_npfield_frame(const NpField &f, const BoundingBoxf3 &mesh_box, const std::string &path)
{
    if (f.mesh_min && (*f.mesh_min - mesh_box.min).cwiseAbs().maxCoeff() > 0.01)
        fail(path, "its mesh_min (" + std::to_string(f.mesh_min->x()) + ", " + std::to_string(f.mesh_min->y()) + ", " +
                       std::to_string(f.mesh_min->z()) + ") is not the sliced mesh's (" + std::to_string(mesh_box.min.x()) + ", " +
                       std::to_string(mesh_box.min.y()) + ", " + std::to_string(mesh_box.min.z()) +
                       "): the field was solved for another placement");
}

NpField load_npfield_for(const std::string &path, const BoundingBoxf3 &slicing_box, const Vec2d &plate_offset)
{
    NpField     f = load_npfield(path);
    const Vec3d shift(plate_offset.x(), plate_offset.y(), 0.);
    check_npfield_frame(f, BoundingBoxf3(slicing_box.min + shift, slicing_box.max + shift), path);
    to_plate_frame(f, -plate_offset);
    return f;
}

Vec2d plate_offset(const PrintObject &object)
{
    const ModelObject &mo = *object.model_object();
    return mo.instances.empty() ? Vec2d::Zero() : Vec2d(mo.instances.front()->get_offset().head<2>());
}

void to_plate_frame(NpField &f, const Vec2d &plate_offset)
{
    const Vec3d shift(plate_offset.x(), plate_offset.y(), 0.);
    f.origin += shift;
    if (f.mesh_min)
        *f.mesh_min += shift;
}

NpLayerField::NpLayerField(NpField field) : m_field(std::move(field))
{
    const NpField &f = m_field;
    size_t bad = f.dims[2];
    for (size_t k = 0; k < f.dims[2] && bad == f.dims[2]; ++k)
        for (size_t i = 0; i < f.dims[0] && bad == f.dims[2]; ++i)
            for (size_t j = 0; j < f.dims[1]; ++j)
                if (std::abs(double(f.phi[f.index(i, j, k)]) - f.node(i, j, k).z()) >= 1e-3) {
                    bad = k;
                    break;
                }
    if (bad == f.dims[2]) {
        m_identity_below = std::numeric_limits<double>::infinity();
        return;
    }
    const double z_bad = f.origin.z() + double(bad) * f.spacing.z() - f.spacing.z();
    const double k     = std::floor((z_bad - f.first_layer) / f.h + 1e-9);
    m_identity_below   = k >= 0. ? f.first_layer + k * f.h : 0.;
}

void NpLayerField::locate(const Vec3d &p, std::array<size_t, 3> &c, Vec3d &t) const
{
    for (int a = 0; a < 3; ++a) {
        const double x = std::clamp((p[a] - m_field.origin[a]) / m_field.spacing[a], 0., double(m_field.dims[size_t(a)] - 1));
        c[size_t(a)]   = std::min(size_t(x), m_field.dims[size_t(a)] - 2);
        t[a]           = x - double(c[size_t(a)]);
    }
}

double NpLayerField::phi(const Vec3d &p) const
{
    std::array<size_t, 3> c;
    Vec3d                 t;
    this->locate(p, c, t);
    double v = 0.;
    for (int q = 0; q < 8; ++q) {
        const size_t a = q & 1, b = (q >> 1) & 1, e = (q >> 2) & 1;
        const double w = (a ? t.x() : 1. - t.x()) * (b ? t.y() : 1. - t.y()) * (e ? t.z() : 1. - t.z());
        v += w * (double(m_field.phi[m_field.index(c[0] + a, c[1] + b, c[2] + e)]) - m_field.node(c[0] + a, c[1] + b, c[2] + e).z());
    }
    return p.z() + v;
}

Vec3d NpLayerField::gradient(const Vec3d &p) const
{
    std::array<size_t, 3> c;
    Vec3d                 t;
    this->locate(p, c, t);
    Vec3d g(0., 0., 1.);
    for (int q = 0; q < 8; ++q) {
        const size_t a = q & 1, b = (q >> 1) & 1, e = (q >> 2) & 1;
        const double v  = double(m_field.phi[m_field.index(c[0] + a, c[1] + b, c[2] + e)]) - m_field.node(c[0] + a, c[1] + b, c[2] + e).z();
        const double wx = a ? t.x() : 1. - t.x(), wy = b ? t.y() : 1. - t.y(), wz = e ? t.z() : 1. - t.z();
        g.x() += v * (a ? 1. : -1.) * wy * wz / m_field.spacing.x();
        g.y() += v * wx * (b ? 1. : -1.) * wz / m_field.spacing.y();
        g.z() += v * wx * wy * (e ? 1. : -1.) / m_field.spacing.z();
    }
    for (int a = 0; a < 3; ++a) {
        const double x = (p[a] - m_field.origin[a]) / m_field.spacing[a];
        if (x < 0. || x > double(m_field.dims[size_t(a)] - 1))
            g[a] = a == 2 ? 1. : 0.;
    }
    return g;
}

}
