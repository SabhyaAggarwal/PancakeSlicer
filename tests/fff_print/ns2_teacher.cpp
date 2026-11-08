#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"

#include "libslic3r/Exception.hpp"
#include "libslic3r/Nonplanar/FieldObjective.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Nonplanar/NpField.hpp"
#include "libslic3r/Nonplanar/OneShotField.hpp"
#include "libslic3r/Nonplanar/Npy.hpp"
#include "libslic3r/miniz_extension.hpp"

#include <boost/nowide/fstream.hpp>
#include <miniz.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <regex>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::Nonplanar;

namespace {

[[noreturn]] void usage(const std::string &error)
{
    std::cerr << "error: " << error << "\n"
              << "usage: ns2_teacher --geom PART.npz --machine DESCRIPTOR.json (-o FIELD.npz | --eval [FIELD.npz])\n"
                 "       [--from FIELD.npz] [--budget S] [--starts N] [--iterations N] [--start-iterations N]\n"
                 "       [--covers proxy|descriptor|none] [--time-base S] [--planar-adaptive] [--polish K]\n"
                 "       [--verify-cmd \"SLICE COMMAND with {field}\"] [--verify-time-ratio R]\n"
                 "       [--stair-k K] [--stair-s-min MM] [--stair-theta-min DEG]   (the stair groove term, ObjectiveWeights)\n";
    std::exit(2);
}

double rad(double deg) { return deg * M_PI / 180.; }

struct Verified
{
    long   hits = -1;
    double time = -1.;
    double real_j = -1.;
};
Verified verify(std::string cmd, const std::string &field)
{
    for (size_t at; (at = cmd.find("{field}")) != std::string::npos;)
        cmd.replace(at, 7, field);
    std::unique_ptr<FILE, int (*)(FILE *)> pipe(popen((cmd + " 2>&1").c_str(), "r"), pclose);
    if (!pipe)
        return {};
    std::array<char, 4096> buf;
    std::string            out;
    while (fgets(buf.data(), int(buf.size()), pipe.get()) != nullptr)
        out += buf.data();
    Verified    v;
    std::smatch m;
    if (std::regex_search(out, m, std::regex("clearance_hit_moves ([0-9]+)")))
        v.hits = std::stol(m[1]);
    if (std::regex_search(out, m, std::regex("estimated_time_s ([0-9.eE+-]+)")))
        v.time = std::stod(m[1]);
    if (std::regex_search(out, m, std::regex("real_J ([0-9.eE+-]+)")))
        v.real_j = std::stod(m[1]);
    return v;
}

struct Part
{
    ObjectiveGeometry geom;
    Vec3d             mesh_min { Vec3d::Zero() };
    std::string       mesh_sha256;
};

Part load_geometry(const std::string &path)
{
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!open_zip_reader(&zip, path))
        throw RuntimeError("cannot open " + path);
    struct Closer { mz_zip_archive *z; ~Closer() { close_zip_reader(z); } } closer { &zip };
    auto           rd = [&](const char *name, bool required = true) { return read_npz_array(zip, name, required, path); };
    Part           part;
    ObjectiveGeometry &g = part.geom;
    const NpyArray p = rd("p"), n = rd("n"), a = rd("a"), cls = rd("cls"), origin = rd("origin"), spacing = rd("spacing"),
                   dims = rd("dims"), occ = rd("occ"), shell = rd("shell"), mesh_min = rd("mesh_min"), mesh_max = rd("mesh_max"), sha = rd("mesh_sha256", false);
    if (p.values.size() != 3 * a.values.size() || n.values.size() != p.values.size() || cls.values.size() != a.values.size() ||
        origin.values.size() != 3 || spacing.values.size() != 3 || dims.values.size() != 3 || mesh_min.values.size() != 3 || mesh_max.values.size() != 3)
        throw RuntimeError(path + ": arrays of inconsistent sizes");
    for (size_t s = 0; s < a.values.size(); ++s) {
        g.p.emplace_back(p.values[3 * s], p.values[3 * s + 1], p.values[3 * s + 2]);
        g.n.emplace_back(n.values[3 * s], n.values[3 * s + 1], n.values[3 * s + 2]);
        g.a.push_back(a.values[s]);
        g.cls.push_back(int8_t(cls.values[s]));
    }
    g.origin  = Vec3d(origin.values[0], origin.values[1], origin.values[2]);
    g.spacing = Vec3d(spacing.values[0], spacing.values[1], spacing.values[2]);
    g.dims    = { size_t(dims.values[0]), size_t(dims.values[1]), size_t(dims.values[2]) };
    if (occ.values.size() != g.nodes() || shell.values.size() != g.nodes())
        throw RuntimeError(path + ": occ and shell do not fill the grid");
    for (double v : occ.values)
        g.occ.push_back(uint8_t(v != 0.));
    for (double v : shell.values)
        g.shell.push_back(float(v));
    part.mesh_min    = Vec3d(mesh_min.values[0], mesh_min.values[1], mesh_min.values[2]);
    g.mesh_min       = part.mesh_min;
    g.mesh_max       = Vec3d(mesh_max.values[0], mesh_max.values[1], mesh_max.values[2]);
    part.mesh_sha256 = sha.text;
    return part;
}

ObjectiveMachine load_machine(const nlohmann::json &md, const std::string &covers)
{
    ObjectiveMachine m;
    const auto &lim = md.at("j_limits"), &proc = md.at("process");
    m.h                  = lim.at("layer_height");
    m.first_layer        = proc.value("initial_layer_print_height", m.h);
    m.thickness_min      = lim.at("thickness_mm")[0];
    m.thickness_max      = lim.at("thickness_mm")[1];
    m.printable_overhang = rad(lim.at("printable_overhang_deg"));
    m.rotary             = !md.at("rotary").is_null();
    m.tilt_max           = rad(double(lim.value("tool_tilt_max_deg", 0.)) - 5.);
    if (!m.rotary) {
        const auto &ls = lim.at("layer_slope_max");
        for (int k = 0; k < 24; ++k)
            m.slope_cap[size_t(k)] = rad(std::min(double(ls.at("head_per_azimuth_by_reach_deg").at("5.0")[size_t(k)]), ls.value("road_sawtooth_deg", 90.)));
    }
    const nlohmann::json cover = md.contains("cover") && md.at("cover").is_object() ? md.at("cover") : nlohmann::json::object();
    if (covers == "proxy") {
        if (cover.contains("shell_rms_mm") && !cover.at("shell_rms_mm").is_null()) {
            m.cover.shell_rms       = cover.at("shell_rms_mm");
            m.cover.shell_slope_cap = rad(cover.value("shell_slope_max_deg", double(lim.at("layer_slope_max").at("deg"))));
        }
        if (cover.contains("skin_rms_mm") && !cover.at("skin_rms_mm").is_null()) {
            m.cover.skin_rms = cover.at("skin_rms_mm");
            m.cover.skin_classes = 0;
            const nlohmann::json classes = cover.contains("skin") && cover.at("skin").contains("classes") ? cover.at("skin").at("classes") : nlohmann::json::array({ "wall" });
            for (const auto &c : classes)
                m.cover.skin_classes |= c == "up" ? 1 : c == "wall" ? 2 : 4;
        }
        m.cover_planar = m.cover;
        m.cover_shells = m.cover;
    } else if (covers == "descriptor") {
        auto read = [](const nlohmann::json &c) {
            ObjectiveCover out;
            if (!c.is_object())
                return out;
            if (c.contains("shell") && c.at("shell").is_object()) {
                out.shell_rms       = c.at("shell").at("shell_rms_mm");
                out.shell_slope_cap = rad(c.at("shell").at("slope_cap_deg"));
            }
            if (c.contains("skin") && c.at("skin").is_object()) {
                const auto &skin = c.at("skin");
                out.skin_rms     = skin.at("skin_rms_mm");
                out.skin_tilt_lo = rad(skin.at("faces").at("tilt_from_up_deg")[0]);
                out.skin_tilt_hi = rad(skin.at("faces").at("tilt_from_up_deg")[1]);
                out.skin_min_nz  = skin.at("faces").at("min_nz");
                out.skin_time    = skin.value("skin_time_s_per_mm2", 0.17);
            }
            return out;
        };
        const bool paths = cover.contains("paths");
        m.cover          = read(paths ? cover.at("paths").value("field", nlohmann::json()) : cover);
        m.cover_planar   = read(paths ? cover.at("paths").value("planar", nlohmann::json()) : cover);
        m.cover_shells   = read(paths ? cover.at("paths").value("field_shells", nlohmann::json()) : cover);
    } else if (covers != "none")
        usage("--covers is proxy, descriptor or none");
    m.line_width     = proc.at("line_width");
    const std::string density = proc.at("sparse_infill_density").is_string() ? std::string(proc.at("sparse_infill_density")) :
                                                                                 std::to_string(double(proc.at("sparse_infill_density")));
    m.infill_density = std::stod(density) / 100.;
    m.shell_speed    = 0.5 * (double(proc.at("outer_wall_speed")) + double(proc.at("inner_wall_speed")));
    m.sparse_speed   = proc.at("sparse_infill_speed");
    m.top_speed         = proc.value("top_surface_speed", 0.);
    m.first_layer_speed = proc.value("initial_layer_speed", 0.);
    m.max_volumetric = proc.at("filament_max_volumetric_speed");
    {
        const double      d = proc.at("nozzle_diameter");
        const std::string v = proc.contains("infill_combination_max_layer_height") ?
                                  (proc.at("infill_combination_max_layer_height").is_string() ?
                                       std::string(proc.at("infill_combination_max_layer_height")) :
                                       std::to_string(double(proc.at("infill_combination_max_layer_height")))) :
                                  std::string("100%");
        const double cap = !v.empty() && v.back() == '%' ? d * std::stod(v) / 100. : std::stod(v);
        m.infill_combine = cap > 0. ? std::min(cap, d) : d;
    }
    m.travel_speed   = proc.value("travel_speed", 80.);
    m.min_layer_time = proc.value("slow_down_layer_time", 8.);
    m.road_factor    = m.rotary ? 1.30 : 2.42;
    if (m.rotary)
        for (const auto &ax : md.at("rotary").at("axes")) {
            const std::string name = ax.at("name");
            if (name == "C") {
                m.c_rate       = rad(ax.at("max_speed_deg_s"));
                m.c_continuous = ax.value("continuous", false);
            } else
                m.b_rate = rad(ax.at("max_speed_deg_s"));
        }
    if (md.at("bed").contains("plate_radius") && !md.at("bed").at("plate_radius").is_null())
        m.plate_radius = md.at("bed").at("plate_radius");
    return m;
}

nlohmann::json terms_json(const ObjectiveTerms &t)
{
    nlohmann::json o;
    for (const auto &[k, v] : t.named())
        o[k] = v;
    o["feasible"] = t.feasible();
    o["H_head_probes"] = t.H_head_probes;
    return o;
}

}

int main(int argc, char **argv)
{
    std::string geom_path, machine_path, out, from, eval, covers = "descriptor", verify_cmd;
    double      verify_time_ratio = 1.15;
    double      verify_time_base  = 0.;
    bool        evaluate = false;
    double      time_base = 0.;
    ObjectiveParams  params;
    ObjectiveWeights w;
    for (int i = 1; i < argc; ++i) {
        const std::string a    = argv[i];
        auto              next = [&]() -> std::string {
            if (i + 1 >= argc)
                usage(a + " needs a value");
            return argv[++i];
        };
        if (a == "--geom") geom_path = next();
        else if (a == "--machine") machine_path = next();
        else if (a == "-o") out = next();
        else if (a == "--from") from = next();
        else if (a == "--eval") {
            evaluate = true;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                eval = next();
        } else if (a == "--budget") params.budget = std::atof(next().c_str());
        else if (a == "--starts") params.max_starts = std::atoi(next().c_str());
        else if (a == "--iterations") params.iterations = std::atoi(next().c_str());
        else if (a == "--start-iterations") params.start_iterations = std::atoi(next().c_str());
        else if (a == "--covers") covers = next();
        else if (a == "--planar-adaptive") params.planar_adaptive = true;
        else if (a == "--w-sup") w.sup = std::stod(next());
        else if (a == "--tilt-sparsity") params.tilt_sparsity = std::stod(next());
        else if (a == "--polish") {
            params.iterations    = std::atoi(next().c_str());
            params.multi_start   = false;
            params.warm_adaptive = false;
            params.local_rounds  = 0;
            params.level_rounds  = 0;
        }
        else if (a == "--verify-cmd") verify_cmd = next();
        else if (a == "--verify-time-ratio") verify_time_ratio = std::stod(next());
        else if (a == "--time-base") time_base = std::atof(next().c_str());
        else if (a == "--stair-k") w.stair_k = std::atof(next().c_str());
        else if (a == "--stair-s-min") w.stair_s_min = std::atof(next().c_str());
        else if (a == "--stair-theta-min") w.stair_theta_min = std::atof(next().c_str()) * M_PI / 180.;
        else if (a == "--verify-time-base") verify_time_base = std::atof(next().c_str());
        else usage("unknown option " + a);
    }
    if (geom_path.empty() || machine_path.empty() || (out.empty() && !evaluate))
        usage("--geom, --machine and -o or --eval are required");
    try {
        const Part             part = load_geometry(geom_path);
        boost::nowide::ifstream mf(machine_path);
        const nlohmann::json   md   = nlohmann::json::parse(mf);
        ObjectiveMachine       m    = load_machine(md, covers);
        {
            const nlohmann::json &hd = md.at("head");
            HeadEnvelope head;
            head.margin         = hd.value("margin", 0.);
            head.lateral_margin = hd.value("lateral_margin", 0.);
            if (hd.contains("nozzle") && hd.at("nozzle").is_object())
                head.tip_allowance = { hd.at("nozzle").value("allowance_radius", 0.), hd.at("nozzle").value("allowance_height", 0.) };
            for (const auto &b : hd.at("boxes")) {
                HeadBox box;
                box.min        = Vec3d(b.at("min")[0], b.at("min")[1], b.at("min")[2]);
                box.max        = Vec3d(b.at("max")[0], b.at("max")[1], b.at("max")[2]);
                box.nozzle     = b.value("nozzle", false);
                box.infinite_x = b.value("infinite_x", false);
                box.infinite_y = b.value("infinite_y", false);
                head.boxes.push_back(box);
            }
            const ObjectiveGeometry &pg = part.geom;
            const Vec3d span = Vec3d(double(pg.dims[0]), double(pg.dims[1]), double(pg.dims[2])).cwiseProduct(pg.spacing);
            m.head = head_points(head, pg.spacing.minCoeff(), span.norm());
        }
        if (time_base > 0.)
            m.time_base_zonly = m.time_base_curved = time_base;
        const ObjectiveGeometry &g  = part.geom;
        std::vector<float> phi(g.nodes());
        const std::string start = evaluate ? eval : from;
        bool              shells = false;
        double            field_h = m.h, field_first = m.first_layer;
        if (!start.empty()) {
            const NpField f = load_npfield(start);
            field_h = f.h, field_first = f.first_layer;
            shells = std::any_of(f.terms.begin(), f.terms.end(), [](const auto &kv) { return kv.first == "stat_top_shells" && kv.second > 0.5; }) &&
                     in_plane_spread(f) >= OneShotState::ZOnlySpread;
            if (f.dims != g.dims || (f.origin - g.origin).cwiseAbs().maxCoeff() > 1e-6 || (f.spacing - g.spacing).cwiseAbs().maxCoeff() > 1e-9)
                throw RuntimeError(start + ": the field's grid is not the geometry's");
            check_npfield_frame(f, BoundingBoxf3(part.mesh_min, part.mesh_min), start);
            phi = f.phi;
        } else
            for (size_t i = 0; i < g.dims[0]; ++i)
                for (size_t j = 0; j < g.dims[1]; ++j)
                    for (size_t k = 0; k < g.dims[2]; ++k)
                        phi[g.index(i, j, k)] = float(g.origin.z() + double(k) * g.spacing.z());
        const auto             t0 = std::chrono::steady_clock::now();
        ObjectiveTerms         t;
        if (evaluate) {
            if (m.time_base_zonly <= 0.) {
                std::vector<float> planar(g.nodes());
                for (size_t i = 0; i < g.dims[0]; ++i)
                    for (size_t j = 0; j < g.dims[1]; ++j)
                        for (size_t k = 0; k < g.dims[2]; ++k)
                            planar[g.index(i, j, k)] = float(g.origin.z() + double(k) * g.spacing.z());
                const ObjectiveTerms tp = evaluate_objective(g, m, w, planar, false);
                m.time_base_zonly       = m.time_zonly(tp);
                m.time_base_curved      = m.time_curved(tp);
            }
            ObjectiveMachine mf = m;
            mf.h = field_h, mf.first_layer = field_first;
            if (shells)
                mf.cover = m.cover_shells;
            t = evaluate_objective(g, mf, w, phi, true);
        }
        else {
            t = solve_objective(g, m, w, params, phi, {}, [](const std::string &line) { std::cerr << line << std::endl; });
            NpField f;
            f.origin      = g.origin;
            f.spacing     = g.spacing;
            f.dims        = g.dims;
            f.phi         = phi;
            f.h           = m.h;
            f.first_layer = m.first_layer;
            f.mesh_min    = part.mesh_min;
            f.mesh_sha256 = part.mesh_sha256;
            f.machine     = md.at("id");
            f.solver      = std::string(params.planar_adaptive ? "ns2_teacher planar-adaptive" : "ns2_teacher") + " " + ObjectiveVersion + " (covers " + covers + ")";
            f.terms       = t.named();
            f.terms.emplace_back("stat_j_version", ObjectiveVersionNumber);
            f.proxy_version = ObjectiveVersion;
            save_npfield(out, f);
            if (!verify_cmd.empty()) {
                const std::vector<float> solved = phi;
                std::vector<float>       planar(g.nodes());
                for (size_t i = 0; i < g.dims[0]; ++i)
                    for (size_t j = 0; j < g.dims[1]; ++j)
                        for (size_t k = 0; k < g.dims[2]; ++k)
                            planar[g.index(i, j, k)] = float(g.origin.z() + double(k) * g.spacing.z());
                auto write = [&](double blend, bool shells = false) {
                    for (size_t id = 0; id < f.phi.size(); ++id)
                        f.phi[id] = float(double(planar[id]) + blend * (double(solved[id]) - double(planar[id])));
                    ObjectiveMachine mf = m;
                    if (shells && in_plane_spread(f) >= OneShotState::ZOnlySpread)
                        mf.cover = m.cover_shells;
                    f.terms = evaluate_objective(g, mf, w, f.phi, true).named();
                    f.terms.emplace_back("stat_j_version", ObjectiveVersionNumber);
                    if (shells)
                        f.terms.emplace_back("stat_top_shells", 1.);
                    save_npfield(out, f);
                };
                std::vector<std::pair<double, Verified>> runs;
                auto run = [&](double blend) {
                    write(blend);
                    const Verified v = verify(verify_cmd, out);
                    std::cerr << "verify blend " << blend << ": " << v.hits << " hit moves, " << v.time << " s, real J " << v.real_j << std::endl;
                    runs.emplace_back(blend, v);
                    return v;
                };
                const Verified planar_run = run(0.);
                const double   time_cap   = verify_time_ratio * (verify_time_base > 0. ? verify_time_base : planar_run.time);
                auto in_budget = [&](const Verified &v) { return v.hits == 0 && v.time >= 0. && (planar_run.time < 0. || v.time <= time_cap); };
                auto bisect = [&](double hi, Verified at_hi, auto ok) {
                    if (ok(at_hi))
                        return std::make_pair(hi, at_hi);
                    double   lo = 0.;
                    Verified at_lo = planar_run;
                    for (int it = 0; it < 5; ++it) {
                        const double   mid = 0.5 * (lo + hi);
                        const Verified v   = run(mid);
                        if (ok(v))
                            lo = mid, at_lo = v;
                        else
                            hi = mid;
                    }
                    return std::make_pair(lo, at_lo);
                };
                const Verified solved_run = run(1.);
                const auto     by_hits    = bisect(1., solved_run, [](const Verified &v) { return v.hits == 0; });
                std::pair<double, Verified> pick;
                if (planar_run.real_j >= 0.) {
                    for (double frac : { 0.75, 0.5, 0.25, 0.125 })
                        if (const double b = frac * by_hits.first;
                            b > 0. && std::none_of(runs.begin(), runs.end(), [b](const auto &r) { return std::abs(r.first - b) < 1e-9; }))
                            run(b);
                    pick = { 0., planar_run };
                    for (const auto &r : runs)
                        if (in_budget(r.second) && r.second.real_j >= 0. && r.second.real_j < pick.second.real_j)
                            pick = r;
                } else
                    pick = bisect(by_hits.first, by_hits.second, in_budget);
                bool     shells = false;
                Verified shelled;
                if (planar_run.real_j >= 0. && pick.first > 0.) {
                    write(pick.first);
                    if (in_plane_spread(f) >= OneShotState::ZOnlySpread) {
                        write(pick.first, true);
                        shelled = verify(verify_cmd, out);
                        std::cerr << "verify blend " << pick.first << " with top shells: " << shelled.hits << " hit moves, " << shelled.time
                                  << " s, real J " << shelled.real_j << std::endl;
                        shells = in_budget(shelled) && shelled.real_j >= 0. && shelled.real_j < pick.second.real_j;
                        if (shells)
                            pick.second = shelled;
                    }
                }
                write(pick.first, shells);
                t    = evaluate_objective(g, m, w, f.phi, true);
                f.terms = t.named();
                f.terms.emplace_back("stat_j_version", ObjectiveVersionNumber);
                if (shells)
                    f.terms.emplace_back("stat_top_shells", 1.);
                f.terms.emplace_back("stat_verify_shells_real_J", shelled.real_j);
                f.terms.emplace_back("stat_verify_blend", pick.first);
                f.terms.emplace_back("stat_verify_blend_hits", by_hits.first);
                f.terms.emplace_back("stat_verify_blend_time", pick.first);
                f.terms.emplace_back("stat_verify_hits", double(pick.second.hits));
                f.terms.emplace_back("stat_verify_time", pick.second.time);
                f.terms.emplace_back("stat_verify_time_base", planar_run.time);
                f.terms.emplace_back("stat_verify_real_J", pick.second.real_j);
                f.terms.emplace_back("stat_verify_real_J_planar", planar_run.real_j);
                for (size_t k = 0; k < runs.size(); ++k) {
                    const std::string key = "stat_verify_run" + std::to_string(k) + "_";
                    f.terms.emplace_back(key + "blend", runs[k].first);
                    f.terms.emplace_back(key + "hits", double(runs[k].second.hits));
                    f.terms.emplace_back(key + "time", runs[k].second.time);
                    f.terms.emplace_back(key + "real_J", runs[k].second.real_j);
                }
                save_npfield(out, f);
            }
        }
        nlohmann::json o = terms_json(t);
        o["time_base"] = m.time_base_zonly;
        o["seconds"]     = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        o["samples"]     = g.p.size();
        std::cout << o.dump(1) << std::endl;
    } catch (const std::exception &ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
