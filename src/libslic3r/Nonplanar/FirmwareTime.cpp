#include "FirmwareTime.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <boost/nowide/cstdio.hpp>
#include <fast_float/fast_float.h>
#include <cstdio>
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {
namespace {
constexpr double Pi = 3.14159265358979323846;
struct Block {
    FirmwareTimedMove move;
    Vec3d unit = Vec3d::Zero();
    std::array<double, 4> jerk{};
    double distance = 0., e = 0., speed = 0., accel = 0., accel_set = 0., jd = 0.;
    double wait = 0., rate = 0.;
    bool kinetic = false, shell = false;
};
double first(const ConfigOptionFloats &opt) { return opt.values.empty() ? 0. : opt.values.front(); }
double word(const GCodeReader::GCodeLine &line, char key, double otherwise = 0.)
{
    std::string_view axis = line.axis_pos(key);
    if (axis.empty()) return otherwise;
    axis.remove_prefix(1);
    while (!axis.empty() && (axis.front() == ' ' || axis.front() == '\t')) axis.remove_prefix(1);
    if (!axis.empty() && axis.front() == '+') axis.remove_prefix(1);
    double value = 0.;
    const auto parsed = fast_float::from_chars(axis.data(), axis.data() + axis.size(), value);
    return parsed.ec == std::errc() && parsed.ptr != axis.data() ? value : otherwise;
}
int integer_after(std::string_view text, std::string_view key, int otherwise)
{
    const size_t at = text.rfind(key);
    if (at == std::string_view::npos) return otherwise;
    text.remove_prefix(at + key.size());
    bool negative = !text.empty() && text.front() == '-';
    if (negative) text.remove_prefix(1);
    int value = 0;
    if (text.empty() || text.front() < '0' || text.front() > '9') return otherwise;
    for (char c : text) {
        if (c < '0' || c > '9') break;
        if (value > 100000000) return otherwise;
        value = value * 10 + c - '0';
    }
    return negative ? -value : value;
}
double component(const Block &b, size_t axis) { return axis == 3 ? b.e : b.unit[axis]; }
double safe_speed(const Block &b)
{
    double speed = b.speed;
    for (size_t axis = 0; axis < 4; ++axis) {
        const double v = std::abs(component(b, axis)) * b.speed;
        if (b.jerk[axis] > 0. && v > b.jerk[axis]) speed = std::min(speed, b.speed * b.jerk[axis] / v);
    }
    return speed;
}
double trapezoid(double d, double a, double entry, double cruise, double exit)
{
    cruise = std::max({cruise, entry, exit});
    const double up = (cruise * cruise - entry * entry) / (2. * a);
    const double down = (cruise * cruise - exit * exit) / (2. * a);
    if (up + down > d) {
        const double peak = std::sqrt(std::max((2. * a * d + entry * entry + exit * exit) * .5, std::max(entry * entry, exit * exit)));
        return (2. * peak - entry - exit) / a;
    }
    return (2. * cruise - entry - exit) / a + (d - up - down) / cruise;
}
void plan(std::vector<Block> &blocks, bool bambu, bool rrf, bool marlin2, size_t buffer)
{
    if (bambu) for (size_t i = 1; i < blocks.size(); ++i) {
        Block &b = blocks[i]; const Block &p = blocks[i - 1];
        const double n1 = p.unit.head<2>().norm(), n2 = b.unit.head<2>().norm();
        if (!p.kinetic || !b.kinetic || n1 <= 1e-9 || n2 <= 1e-9 || b.distance * n2 <= 1e-4) continue;
        const Vec2d u = p.unit.head<2>() / n1, v = b.unit.head<2>() / n2;
        const double turn = (v - u).norm();
        if (turn > 1e-5 && turn < .5) {
            const double angle = std::atan2(u.x() * v.y() - u.y() * v.x(), u.dot(v));
            const double radius = b.distance * n2 * .5 / std::sqrt((1. - std::cos(angle)) * .5);
            b.speed = std::min(b.speed, std::sqrt(b.accel_set * radius));
        }
    }
    if (!rrf) {
        std::deque<double> window; double ahead = 0.;
        for (size_t i = blocks.size(); i-- > 0;) {
            Block &b = blocks[i];
            if (!b.kinetic) { window.clear(); ahead = 0.; continue; }
            window.push_back(b.distance); ahead += b.distance;
            if (window.size() > buffer) { ahead -= window.front(); window.pop_front(); }
            b.speed = std::min(b.speed, std::max(1e-3, std::sqrt(2. * b.accel * ahead)));
        }
    }
    std::vector<double> safe(blocks.size()), entry(blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        const Block &b = blocks[i];
        const bool jd = marlin2 && b.jd > 0.;
        safe[i] = jd ? 0. : safe_speed(b);
        if (i == 0) { entry[i] = safe[i]; continue; }
        const Block &p = blocks[i - 1];
        if (jd) {
            const double np = std::sqrt(p.unit.squaredNorm() + p.e * p.e), nb = std::sqrt(b.unit.squaredNorm() + b.e * b.e);
            double cosine = np * nb > 0. ? -(p.unit.dot(b.unit) + p.e * b.e) / (np * nb) : 0.;
            double v2 = .05 * .05;
            if (cosine <= .999999) {
                cosine = std::max(cosine, -.999999);
                const double sine = std::sqrt(.5 * (1. - cosine));
                v2 = b.accel * b.jd * sine / (1. - sine);
                if (b.distance < 1.) {
                    const double theta = std::acos(-cosine);
                    if (theta > 0.) v2 = std::min(v2, b.distance * b.accel / theta);
                }
            }
            entry[i] = std::sqrt(std::min({v2, b.speed * b.speed, p.speed * p.speed}));
        } else if (rrf) {
            const double v = std::min(b.speed, p.speed); double factor = 1.;
            for (size_t a = 0; a < 4; ++a) {
                const double jump = std::abs(component(b, a) - component(p, a)) * v;
                if (b.jerk[a] > 0. && jump * factor > b.jerk[a]) factor = b.jerk[a] / jump;
            }
            entry[i] = v * factor;
        } else {
            double v = std::min(b.speed, p.speed), factor = 1.; bool limited = false;
            const double smaller = b.speed < p.speed ? b.speed / p.speed : 1.;
            for (size_t a = 0; a < 4; ++a) {
                double exit = component(p, a) * p.speed * smaller, enter = component(b, a) * b.speed;
                if (limited) { exit *= factor; enter *= factor; }
                const double jerk = bambu && a < 3 ? std::abs(enter - exit) : exit > enter ?
                    ((enter > 0. || exit < 0.) ? exit - enter : std::max(exit, -enter)) :
                    ((enter < 0. || exit > 0.) ? enter - exit : std::max(-exit, enter));
                if (b.jerk[a] > 0. && jerk > b.jerk[a]) { factor *= b.jerk[a] / jerk; limited = true; }
            }
            v *= factor;
            if (safe[i - 1] > v * .99 && safe[i] > v * .99) v = safe[i];
            entry[i] = v;
        }
    }
    for (size_t i = blocks.size(); i-- > 0;) {
        const double exit = i + 1 < blocks.size() ? entry[i + 1] : safe[i];
        entry[i] = std::min(entry[i], std::sqrt(exit * exit + 2. * blocks[i].accel * blocks[i].distance));
    }
    for (size_t i = 0; i < blocks.size(); ++i) {
        const double bound = i + 1 < blocks.size() ? entry[i + 1] : safe[i];
        const double exit = std::min(bound, std::sqrt(entry[i] * entry[i] + 2. * blocks[i].accel * blocks[i].distance));
        if (i + 1 < blocks.size()) entry[i + 1] = exit;
        blocks[i].move.seconds = trapezoid(blocks[i].distance, blocks[i].accel, entry[i], blocks[i].speed, exit) + blocks[i].wait;
    }
    std::deque<double> starts; double now = 0., last = -1e9; size_t last_line = 0;
    for (Block &b : blocks) {
        if (b.rate > 0.) {
            double arrival = std::max(last + (b.move.line == last_line ? 0. : 1. / b.rate), starts.size() == 32 ? starts.front() : -1e9);
            double start = now;
            if (b.shell && arrival > now) { b.move.seconds += arrival - now; start = arrival; }
            else arrival = std::min(arrival, now);
            last = arrival; last_line = b.move.line;
            starts.push_back(start); if (starts.size() > 32) starts.pop_front();
        }
        now += b.move.seconds;
    }
}
}

bool firmware_time_supported(const PrintConfig &config, bool bambu)
{
    if (config.nonplanar_kinematics.value != RotaryLayout::None) return false;
    const auto flavor = config.gcode_flavor.value;
    return bambu || flavor == gcfRepRapFirmware || flavor == gcfMarlinFirmware || flavor == gcfMarlinLegacy;
}

FirmwareTime firmware_time(const std::string &file, const PrintConfig &config, bool bambu, const std::function<void()> &cancel)
{
    FirmwareTime out;
    if (!firmware_time_supported(config, bambu)) return out;
    std::FILE *probe = boost::nowide::fopen(file.c_str(), "rb");
    if (probe == nullptr) return out;
    std::fclose(probe);
    const auto flavor = config.gcode_flavor.value;
    const bool rrf = flavor == gcfRepRapFirmware, marlin2 = !bambu && flavor == gcfMarlinFirmware;
    std::array<double, 4> vmax{first(config.machine_max_speed_x),first(config.machine_max_speed_y),first(config.machine_max_speed_z),first(config.machine_max_speed_e)};
    std::array<double, 4> amax{first(config.machine_max_acceleration_x),first(config.machine_max_acceleration_y),first(config.machine_max_acceleration_z),first(config.machine_max_acceleration_e)};
    std::array<double, 4> jerk{first(config.machine_max_jerk_x),first(config.machine_max_jerk_y),first(config.machine_max_jerk_z),first(config.machine_max_jerk_e)};
    double ap = first(config.machine_max_acceleration_extruding); if (ap <= 0.) ap = 1000.;
    double at = first(config.machine_max_acceleration_travel); if (at <= 0.) at = ap;
    double ar = first(config.machine_max_acceleration_retracting); if (ar <= 0.) ar = ap;
    double jd = marlin2 ? first(config.machine_max_junction_deviation) : 0.;
    std::array<double,4> pos{};
    double feed = 0., rate = 0.; bool absolute = true, relative_e = false, shell = false;
    int patch = -1, object = -1; size_t line_id = 0;
    std::vector<Block> blocks;
    auto add = [&](const std::array<double,4> &from, const std::array<double,4> &to) {
        Block b;
        b.move = {Vec3d(from[0],from[1],from[2]),Vec3d(to[0],to[1],to[2]),0.,line_id,object,patch};
        const Vec3d delta = b.move.to - b.move.from; const double de = to[3] - from[3];
        b.distance = delta.norm(); b.move.extruding = b.distance >= 1e-7 && de > 1e-9; b.shell = shell; b.rate = rate; b.jd = jd; b.jerk = jerk;
        if (b.distance < 1e-7) {
            if (std::abs(de) < 1e-9) return;
            b.distance = std::abs(de); b.e = de > 0. ? 1. : -1.;
            b.speed = std::min(feed, vmax[3] > 0. ? vmax[3] : feed); b.accel = de < 0. && amax[3] == 0. ? 1000. : ar;
        } else {
            b.kinetic = true; b.unit = delta / b.distance; b.e = de / b.distance;
            b.speed = feed; b.accel = std::abs(de) > 1e-9 ? ap : at; b.accel_set = ap;
            for (size_t a = 0; a < 4; ++a) {
                const double c = std::abs(to[a] - from[a]); if (c == 0.) continue;
                if (vmax[a] > 0.) b.speed = std::min(b.speed, vmax[a] * b.distance / c);
                if (amax[a] > 0.) b.accel = std::min(b.accel, amax[a] * b.distance / c);
            }
        }
        b.speed = std::max(b.speed, 1e-3); b.accel = std::max(b.accel, 1e-3); blocks.push_back(b);
    };
    GCodeReader reader;
    const bool read = reader.parse_file(file, [&](GCodeReader &, const GCodeReader::GCodeLine &line) {
        ++line_id; if (line_id % 4096 == 0) cancel();
        const std::string_view raw = line.raw(), comment = line.comment();
        if (raw.find(";_NONPLANAR_BEGIN") == 0) { shell = true; patch = integer_after(raw,"patch=",-1); rate = integer_after(raw,"rate=",0); return; }
        if (raw.find(";_NONPLANAR_END") == 0) { shell = false; patch = -1; return; }
        if (comment.find(" printing object ") == 0) object = integer_after(comment,"id:",-1);
        if (comment.find(" stop printing object ") == 0) object = -1;
        const std::string_view cmd = line.cmd();
        if (cmd == "G4" || cmd == "M400") { if (!blocks.empty()) blocks.back().wait += word(line,'S') + word(line,'P') / 1000.; }
        else if (cmd == "M204") { if (line.has('S')) ap = at = word(line,'S'); if(line.has('P')) ap=word(line,'P'); if(line.has('T')) at=word(line,'T'); if(line.has('R')) ar=word(line,'R'); }
        else if (cmd == "M201" || cmd == "M203" || cmd == "M205" || cmd == "M566") {
            auto &target = cmd == "M201" ? amax : cmd == "M203" ? vmax : jerk;
            const double scale = rrf && (cmd == "M203" || cmd == "M566") ? 1. / 60. : 1.;
            for (size_t a = 0; a < 4; ++a) if(line.has("XYZE"[a])) target[a]=word(line,"XYZE"[a])*scale;
            if (marlin2 && cmd == "M205" && line.has('J')) jd=word(line,'J');
        } else if(cmd=="G90") absolute=true;
        else if(cmd=="G91") absolute=false;
        else if(cmd=="M82") relative_e=false;
        else if(cmd=="M83") relative_e=true;
        else if(cmd=="G92") { for(size_t a=0;a<4;++a) if(line.has("XYZE"[a])) pos[a]=word(line,"XYZE"[a]); }
        else if(cmd=="G0" || cmd=="G1" || cmd=="G2" || cmd=="G3") {
            if(line.has('F') && word(line,'F')>0.) feed=word(line,'F')/60.;
            auto next=pos;
            for(size_t a=0;a<3;++a) if(line.has("XYZ"[a])) next[a]=(absolute?0.:pos[a])+word(line,"XYZ"[a]);
            if(line.has('E')) next[3]=(relative_e || !absolute ? pos[3]:0.)+word(line,'E');
            if ((cmd=="G2" || cmd=="G3") && (line.has('I') || line.has('J'))) {
                const double cx=pos[0]+word(line,'I'),cy=pos[1]+word(line,'J');
                const double angle=std::atan2(pos[1]-cy,pos[0]-cx),radius=std::hypot(pos[0]-cx,pos[1]-cy);
                double sweep=std::atan2(next[1]-cy,next[0]-cx)-angle;
                if(cmd=="G2" && sweep>=0.) sweep-=2.*Pi;
                if(cmd=="G3" && sweep<=0.) sweep+=2.*Pi;
                int count=1;
                if(bambu) {
                    if(radius<=.0125) count=std::abs(sweep)<Pi || radius*(1.-std::cos(.5*sweep))<.0125 ? 1:2;
                    else count=std::max(1,int(std::ceil(std::abs(sweep)/(2.*std::acos(1.-.0125/radius)))));
                } else if(marlin2 && config.printer_model.value.rfind("Prusa ",0)==0) {
                    const double segment=std::min(2.,std::max(.1,std::min(std::sqrt(8.*radius*.02),feed/50.)));
                    count=std::max(1,int(std::abs(sweep)*radius/segment+.8));
                } else count=std::max(1,int(std::abs(sweep)*radius));
                auto last=pos;
                for(int k=1;k<=count;++k) {
                    const double f=double(k)/count;
                    std::array<double,4> q{cx+radius*std::cos(angle+sweep*f),cy+radius*std::sin(angle+sweep*f),pos[2]+(next[2]-pos[2])*f,pos[3]+(next[3]-pos[3])*f};
                    if(k==count) q=next;
                    add(last,q);last=q;
                }
            } else add(pos,next);
            pos=next;
        }
    });
    if (!read || blocks.empty()) return out;
    cancel();
    plan(blocks,bambu,rrf,marlin2,bambu || (marlin2 && config.printer_model.value.rfind("Prusa ",0)==0) ? 32:16);
    out.moves.reserve(blocks.size());
    for (auto &b : blocks) {
        if (!std::isfinite(b.move.seconds) || b.move.seconds < 0.) return FirmwareTime{};
        out.seconds += b.move.seconds; out.moves.push_back(std::move(b.move));
    }
    out.valid = out.seconds > 0.;
    return out;
}
}
