#include "libslic3r/IMEXZones.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "PrintConfig.hpp"
#include "BoundingBox.hpp"
#include "Config.hpp"
#include "libslic3r/IMEXHelpers.hpp"

namespace Slic3r {

ImexZoneLayout compute_imex_zone_layout(const DynamicPrintConfig& printer_cfg,
                                        const std::string&        plate_mode,
                                        const std::string&        process_mode,
                                        const BoundingBoxf&       bed_extents)
{
    ImexZoneLayout out;

    auto* is_imex_opt = printer_cfg.option<ConfigOptionBool>("is_imex");
    if (!is_imex_opt || !is_imex_opt->value)
        return out;

    std::string active_mode = plate_mode;
    if (active_mode == kImexPrimaryMode && !process_mode.empty())
        active_mode = process_mode;
    if (active_mode == kImexPrimaryMode || active_mode.empty())
        return out;

    int n_cols  = std::max(1, imex_cfg_int(printer_cfg, "imex_tools_per_gantry"));
    int n_rows  = std::max(1, imex_cfg_int(printer_cfg, "imex_gantry_count"));

    const ImexToolLayout layout = imex_cfg_enum<ImexToolLayout>(printer_cfg, "imex_tool_layout");
    bool flip_x = (layout == ImexToolLayout::FrontRight || layout == ImexToolLayout::RearRight);
    bool flip_y = (layout == ImexToolLayout::RearLeft   || layout == ImexToolLayout::RearRight);

    auto tool_to_phys = [&](int idx) -> std::pair<int,int> {
        int raw_col = idx % n_cols, raw_row = idx / n_cols;
        return { flip_x ? (n_cols - 1 - raw_col) : raw_col,
                 flip_y ? (n_rows - 1 - raw_row) : raw_row };
    };

    int pri_col = 0, pri_row = 0;

    if (n_cols == 1 && n_rows == 1)
        return out;
    const std::string active_tools_str = find_imex_mode(printer_cfg, active_mode).active_tools;

    std::map<int, ImexRole> zone_roles;
    {
        const int primary = imex_primary_tool_for_mode(active_tools_str);
        if (primary >= 0 && primary < n_rows * n_cols)
            zone_roles[primary] = ImexRole::Primary;
        for (const auto& [phys_idx, role] : parse_imex_active_tools(active_tools_str)) {
            if (phys_idx < 0 || phys_idx >= n_rows * n_cols) continue;
            if (phys_idx == primary) continue;
            switch (role) {
            case ImexRole::Copy:
            case ImexRole::Mirror:
                zone_roles[phys_idx] = role;
                break;
            case ImexRole::Primary:
            case ImexRole::Span:
                break;
            }
        }
    }

    if (zone_roles.empty())
        return out;

    for (const auto& [idx, role] : zone_roles) {
        if (role == ImexRole::Primary) { auto [c, r] = tool_to_phys(idx); pri_col = c; pri_row = r;
            out.primary_head = idx; break; }
    }

    if (out.primary_head < 0)
        return ImexZoneLayout{};

    const ImexGantryGrouping grouping =
        group_imex_active_tools_by_gantry(active_tools_str, n_cols);
    std::map<int, const ImexGantryGroup*> group_by_gantry;
    for (const auto& grp : grouping.groups)
        group_by_gantry[grp.gantry_index] = &grp;

    std::set<std::pair<int,int>> copy_cells, mirror_cells;
    for (const auto& [idx, role] : zone_roles) {
        if (role == ImexRole::Primary) continue;

        const int phys_gantry = idx / n_cols;
        auto git = group_by_gantry.find(phys_gantry);
        const ImexGantryGroup* grp = (git == group_by_gantry.end()) ? nullptr : git->second;

        if (grp && grp->aggregate) {
            if (idx != grp->representative_phys) continue;
            auto [rep_c, r] = tool_to_phys(idx);
            (void)rep_c;
            if      (role == ImexRole::Copy)   copy_cells.insert({pri_col, r});
            else if (role == ImexRole::Mirror) mirror_cells.insert({pri_col, r});
        } else {
            auto [c, r] = tool_to_phys(idx);
            if      (role == ImexRole::Copy)   copy_cells.insert({c, r});
            else if (role == ImexRole::Mirror) mirror_cells.insert({c, r});
        }
    }

    std::set<std::pair<int,int>> all_secondary;
    for (auto& p : copy_cells)   all_secondary.insert(p);
    for (auto& p : mirror_cells) all_secondary.insert(p);

    double x_min = bed_extents.min(0), x_max = bed_extents.max(0);
    double y_min = bed_extents.min(1), y_max = bed_extents.max(1);

    std::vector<int> active_cols_v, active_rows_v;
    {
        std::set<int> ac_set, ar_set;
        for (const auto& [idx, role] : zone_roles) {
            auto [c, r] = tool_to_phys(idx);
            ac_set.insert(c); ar_set.insert(r);
        }
        active_cols_v.assign(ac_set.begin(), ac_set.end());
        active_rows_v.assign(ar_set.begin(), ar_set.end());
    }
    int n_active_cols = std::max(1, (int)active_cols_v.size());
    int n_active_rows = std::max(1, (int)active_rows_v.size());

    std::map<int,int> col_to_zone, row_to_zone;
    for (int k = 0; k < n_active_cols; ++k) col_to_zone[active_cols_v[k]] = k;
    for (int k = 0; k < n_active_rows; ++k) row_to_zone[active_rows_v[k]] = k;

    double zone_w = (x_max - x_min) / n_active_cols;
    double zone_h = (y_max - y_min) / n_active_rows;

    int pri_col_k = col_to_zone.count(pri_col) ? col_to_zone[pri_col] : 0;
    int pri_row_k = row_to_zone.count(pri_row) ? row_to_zone[pri_row] : 0;

    for (const auto& [idx, role] : zone_roles) {
        auto [c, r] = tool_to_phys(idx);
        int ck = col_to_zone.count(c) ? col_to_zone.at(c) : 0;
        int rk = row_to_zone.count(r) ? row_to_zone.at(r) : 0;
        out.head_zone_centers[idx] = Vec2d(
            x_min + (ck + 0.5) * zone_w,
            y_min + (rk + 0.5) * zone_h);
    }

    bool has_row_sep = false, has_col_sep = false;
    for (const auto& [sc, sr] : all_secondary) {
        if (sr != pri_row) has_row_sep = true;
        if (sc != pri_col) has_col_sep = true;
    }

    auto make_boxes = [&](const std::set<std::pair<int,int>>& cells) -> std::vector<BoundingBoxf> {
        std::vector<BoundingBoxf> boxes;
        if (cells.empty()) return boxes;
        auto ck = [&](int c) { return col_to_zone.count(c) ? col_to_zone.at(c) : 0; };
        auto rk = [&](int r) { return row_to_zone.count(r) ? row_to_zone.at(r) : 0; };
        if (has_row_sep && !has_col_sep) {
            std::set<int> rows; for (auto& [c,r] : cells) rows.insert(r);
            for (int sr : rows) {
                int k = rk(sr);
                boxes.emplace_back(Vec2d(x_min, y_min + k*zone_h), Vec2d(x_max, y_min + (k+1)*zone_h));
            }
        } else if (has_col_sep && !has_row_sep) {
            std::set<int> cols; for (auto& [c,r] : cells) cols.insert(c);
            for (int sc : cols) {
                int k = ck(sc);
                boxes.emplace_back(Vec2d(x_min + k*zone_w, y_min), Vec2d(x_min + (k+1)*zone_w, y_max));
            }
        } else {
            for (auto& [sc,sr] : cells) {
                int ck_ = ck(sc), rk_ = rk(sr);
                boxes.emplace_back(Vec2d(x_min + ck_*zone_w, y_min + rk_*zone_h),
                                   Vec2d(x_min + (ck_+1)*zone_w, y_min + (rk_+1)*zone_h));
            }
        }
        return boxes;
    };

    out.copy_zones   = make_boxes(copy_cells);
    out.mirror_zones = make_boxes(mirror_cells);

    auto push_secondary_box = [&](const std::vector<BoundingBoxf>& boxes) {
        for (const auto& b : boxes)
            out.secondary_zone_boxes.emplace_back(Vec3d(b.min.x(), b.min.y(), -1.0),
                                                  Vec3d(b.max.x(), b.max.y(), 1e4));
    };
    push_secondary_box(out.copy_zones);
    push_secondary_box(out.mirror_zones);

    double carriage_w = imex_cfg_float(printer_cfg, "imex_nozzle_clearance_x");
    double carriage_h = imex_cfg_float(printer_cfg, "imex_nozzle_clearance_y");
    double margin     = imex_cfg_float(printer_cfg, "imex_carriage_margin");

    double pz_x0 = has_col_sep ? x_min + pri_col_k       * zone_w : x_min;
    double pz_x1 = has_col_sep ? x_min + (pri_col_k + 1) * zone_w : x_max;
    double pz_y0 = has_row_sep ? y_min + pri_row_k        * zone_h : y_min;
    double pz_y1 = has_row_sep ? y_min + (pri_row_k + 1)  * zone_h : y_max;
    out.primary_zone_box = BoundingBoxf(Vec2d(pz_x0, pz_y0), Vec2d(pz_x1, pz_y1));

    auto add_strip = [&](double sx0, double sx1, double sy0, double sy1) {
        out.collision_zones.emplace_back(Vec3d(sx0, sy0, -1.0), Vec3d(sx1, sy1, 1e4));
    };
    auto add_margin_band = [&](double sx0, double sx1, double sy0, double sy1) {
        out.margin_bands.emplace_back(Vec2d(sx0, sy0), Vec2d(sx1, sy1));
    };

    bool has_right_sec = false, has_left_sec  = false;
    bool has_top_sec   = false, has_bottom_sec = false;
    for (const auto& [sc, sr] : mirror_cells) {
        int zc = col_to_zone.count(sc) ? col_to_zone.at(sc) : -1;
        int zr = row_to_zone.count(sr) ? row_to_zone.at(sr) : -1;
        if (zc < 0 || zr < 0) continue;
        if (zr == pri_row_k && zc == pri_col_k + 1) has_right_sec = true;
        if (zr == pri_row_k && zc == pri_col_k - 1) has_left_sec  = true;
        if (zr == pri_row_k + 1 && sc == pri_col) has_top_sec    = true;
        if (zr == pri_row_k - 1 && sc == pri_col) has_bottom_sec = true;
    }

    if (carriage_w > 0.0) {
        if (has_right_sec) {
            double bnd_x = x_min + (pri_col_k + 1) * zone_w;
            double strip_inner = bnd_x - carriage_w;
            add_strip(strip_inner, bnd_x, pz_y0, pz_y1);
            if (margin > 0.0)
                add_margin_band(strip_inner - margin, strip_inner, pz_y0, pz_y1);
        }
        if (has_left_sec) {
            double bnd_x = x_min + pri_col_k * zone_w;
            double strip_inner = bnd_x + carriage_w;
            add_strip(bnd_x, strip_inner, pz_y0, pz_y1);
            if (margin > 0.0)
                add_margin_band(strip_inner, strip_inner + margin, pz_y0, pz_y1);
        }
    }

    if (carriage_h > 0.0) {
        if (has_top_sec) {
            double bnd_y = y_min + (pri_row_k + 1) * zone_h;
            double strip_inner = bnd_y - carriage_h;
            add_strip(pz_x0, pz_x1, strip_inner, bnd_y);
            if (margin > 0.0)
                add_margin_band(pz_x0, pz_x1, strip_inner - margin, strip_inner);
        }
        if (has_bottom_sec) {
            double bnd_y = y_min + pri_row_k * zone_h;
            double strip_inner = bnd_y + carriage_h;
            add_strip(pz_x0, pz_x1, bnd_y, strip_inner);
            if (margin > 0.0)
                add_margin_band(pz_x0, pz_x1, strip_inner, strip_inner + margin);
        }
    }

    return out;
}

}
