#include "CrealityPrintAgent.hpp"
#include "CrealityPrint.hpp"
#include "MoonrakerPrinterAgent.hpp"
#include "IPrinterAgent.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/GUI_App.hpp"

#include <boost/log/trivial.hpp>
#include "libslic3r/Config.hpp"
#include <exception>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <utility>

using json = nlohmann::json;

namespace Slic3r {

namespace {

constexpr const char* CrealityPrintAgent_VERSION = "0.1.0";

std::string to_lower(std::string s)
{
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool has_visible_base_preset(const PresetCollection& filaments, const std::string& filament_id)
{
    for (const auto& p : filaments.get_presets()) {
        if (p.is_visible && p.is_compatible
            && filaments.get_preset_base(p) == &p
            && p.filament_id == filament_id)
            return true;
    }
    return false;
}

std::vector<std::string> name_words(const std::string& name)
{
    std::vector<std::string> words;
    std::string              word;
    for (char c : name.substr(0, name.find('@'))) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (!word.empty()) {
            words.push_back(word);
            word.clear();
        }
    }
    if (!word.empty())
        words.push_back(word);
    return words;
}

}
std::string CrealityPrintAgent::match_filament_preset(const PresetCollection& filaments,
                                                      const std::string&      vendor,
                                                      const std::string&      brand_name,
                                                      const std::string&      base_type)
{
    const std::string vendor_lower = to_lower(vendor);
    const std::string brand_lower  = to_lower(brand_name);
    const std::string type_lower   = to_lower(base_type);

    std::set<std::string> spool_words{"generic"};
    for (const std::string& src : {brand_lower, vendor_lower, type_lower})
        for (auto& w : name_words(src))
            spool_words.insert(std::move(w));

    struct Match {
        const Preset* preset;
        int           score;
        bool          is_user;
    };
    std::vector<Match> matches;

    int considered = 0;
    for (const auto& p : filaments.get_presets()) {
        if (!p.is_visible || !p.is_compatible) continue;
        ++considered;

        std::string preset_type;
        if (const auto* ft = p.config.option<ConfigOptionStrings>("filament_type"))
            if (!ft->values.empty()) preset_type = ft->values.front();
        if (to_lower(preset_type) != type_lower) continue;

        const std::string name_lower = to_lower(p.name);
        int score = 0;
        if (!brand_lower.empty() && name_lower.find(brand_lower) != std::string::npos)
            score += 20;
        if (!vendor_lower.empty()
            && ((p.vendor != nullptr && to_lower(p.vendor->name) == vendor_lower)
                || name_lower.find(vendor_lower) != std::string::npos))
            score += 10;

        if (score == 0) continue;

        for (const auto& w : name_words(p.name))
            if (spool_words.count(w) == 0)
                score -= 5;

        matches.push_back({&p, score, !p.is_system && !p.is_default});
    }

    if (matches.empty()) {
        const std::string fallback = filaments.filament_id_by_type(base_type);
        const bool        fallback_ok = has_visible_base_preset(filaments, fallback);
        BOOST_LOG_TRIVIAL(info)
            << "CrealityPrintAgent: no preset scored for spool {" << vendor << " "
            << brand_name << " (" << base_type << ")} after considering " << considered
            << " presets; falling back to generic preset id \"" << fallback << "\""
            << (fallback_ok ? "" : " (NOT visible — returning empty)");
        return fallback_ok ? fallback : std::string();
    }

    std::sort(matches.begin(), matches.end(),
              [](const Match& a, const Match& b) {
                  if (a.score   != b.score)   return a.score > b.score;
                  if (a.is_user != b.is_user) return !a.is_user;
                  return a.preset->name < b.preset->name;
              });

    BOOST_LOG_TRIVIAL(info)
        << "CrealityPrintAgent: matched spool {" << vendor << " " << brand_name
        << " (" << base_type << ")} -> preset \"" << matches.front().preset->name
        << "\" (score=" << matches.front().score
        << ", " << matches.size() << " candidate(s) of " << considered << " considered)";

    return matches.front().preset->filament_id;
}

CrealityPrintAgent::CrealityPrintAgent(std::string log_dir)
    : MoonrakerPrinterAgent(std::move(log_dir))
{
}

AgentInfo CrealityPrintAgent::get_agent_info_static()
{
    return AgentInfo{
        "crealityprint",
        "CrealityPrint",
        CrealityPrintAgent_VERSION,
        "Creality K-series printer agent (CFS-aware filament sync)"
    };
}

std::string CrealityPrintAgent::normalize_filament_type(const std::string& filament_type)
{
    static const std::vector<std::string> bases = {
        "PETG", "PET", "PLA", "ABS", "ASA", "TPU", "PC", "PA", "PVA", "HIPS"
    };
    for (const auto& base : bases) {
        if (filament_type.rfind(base, 0) == 0) return base;
    }
    return filament_type;
}

bool CrealityPrintAgent::parse_cfs_response(const std::string&    response,
                                            std::vector<CFSSlot>& slots,
                                            int&                  box_count,
                                            std::string&          error)
{

    slots.clear();
    box_count = 0;

    if (response.empty()) {
        error = "empty response";
        return false;
    }

    json resp;
    try {
        resp = json::parse(response);
    } catch (const std::exception& e) {
        error = std::string("JSON parse error: ") + e.what();
        return false;
    }

    if (!resp.contains("boxsInfo") || !resp["boxsInfo"].contains("materialBoxs")) {
        error = "invalid schema (missing boxsInfo.materialBoxs)";
        return false;
    }

    int cfs_count = 0;
    for (const auto& box : resp["boxsInfo"]["materialBoxs"]) {
        const int box_st   = box.value("state", 0);
        const int box_type = box.value("type",  0);
        if (box_st != 1)   continue;
        if (box_type != 0) continue;

        const int cfs_index = cfs_count++;

        if (!box.contains("materials") || !box["materials"].is_array())
            continue;

        for (const auto& mat : box["materials"]) {
            const int s_state = mat.value("state", 0);
            const std::string s_vendor = mat.value("vendor", std::string());
            const std::string s_type   = mat.value("type",   std::string());
            if (s_state == 0)                       continue;
            if (s_vendor.empty() && s_type.empty()) continue;

            CFSSlot s;
            s.box_id        = cfs_index;
            s.slot_id       = mat.value("id",     0);
            s.vendor        = s_vendor;
            s.brand_name    = mat.value("name",   "");
            s.filament_type = s_type;
            s.color_hex     = mat.value("color",  "#FFFFFF");

            if (s.color_hex.size() == 8 && s.color_hex[0] == '#')
                s.color_hex = "#" + s.color_hex.substr(2);

            slots.push_back(std::move(s));
        }
    }

    box_count = cfs_count;
    return true;
}

bool CrealityPrintAgent::fetch_filament_info(std::string dev_id, FilamentSyncMode )
{
    if (device_info.dev_ip.empty()) {
        BOOST_LOG_TRIVIAL(warning)
            << "CrealityPrintAgent::fetch_filament_info: no device IP, falling back to base agent";
        return MoonrakerPrinterAgent::fetch_filament_info(std::move(dev_id));
    }

    DynamicPrintConfig cfg;
    cfg.set_key_value("print_host",                  new ConfigOptionString("http://" + device_info.dev_ip));
    cfg.set_key_value("print_host_webui",            new ConfigOptionString(""));
    cfg.set_key_value("printhost_cafile",            new ConfigOptionString(""));
    cfg.set_key_value("printhost_port",              new ConfigOptionString(""));
    cfg.set_key_value("printhost_apikey",            new ConfigOptionString(device_info.api_key));
    cfg.set_key_value("printhost_ssl_ignore_revoke", new ConfigOptionBool(false));

    CrealityPrint host(&cfg);

    if (!host.supports_multi_color_print()) {
        BOOST_LOG_TRIVIAL(info)
            << "CrealityPrintAgent: " << host.model_name()
            << " is not CFS-capable, deferring to base Moonraker agent";
        return MoonrakerPrinterAgent::fetch_filament_info(std::move(dev_id));
    }

    BOOST_LOG_TRIVIAL(info)
        << "CrealityPrintAgent: querying CFS slots on " << host.model_name();

    const std::string response = host.query_boxes_info();

    std::vector<CFSSlot> slots;
    int                  box_count = 0;
    std::string          parse_err;
    if (!parse_cfs_response(response, slots, box_count, parse_err)) {
        BOOST_LOG_TRIVIAL(warning)
            << "CrealityPrintAgent: CFS query failed (" << parse_err << "), "
            << "falling back to base agent";
        return MoonrakerPrinterAgent::fetch_filament_info(std::move(dev_id));
    }

    if (box_count == 0) {
        BOOST_LOG_TRIVIAL(info)
            << "CrealityPrintAgent: no active CFS boxes, deferring to base agent";
        return MoonrakerPrinterAgent::fetch_filament_info(std::move(dev_id));
    }

    BOOST_LOG_TRIVIAL(info)
        << "CrealityPrintAgent: " << box_count << " CFS box(es), "
        << slots.size() << " loaded slot(s)";

    std::map<std::pair<int, int>, const CFSSlot*> by_position;
    for (const auto& s : slots)
        by_position[{s.box_id, s.slot_id}] = &s;

    auto* bundle = GUI::wxGetApp().preset_bundle;

    const int max_slots = box_count * 4;
    std::vector<AmsTrayData> trays;
    trays.reserve(max_slots);

    for (int box = 0; box < box_count; ++box) {
        for (int idx = 0; idx < 4; ++idx) {
            AmsTrayData tray;
            tray.slot_index = box * 4 + idx;

            auto it = by_position.find({box, idx});
            if (it == by_position.end()) {
                tray.has_filament = false;
                trays.push_back(std::move(tray));
                continue;
            }

            const CFSSlot& s = *it->second;
            tray.has_filament = true;
            tray.tray_type    = normalize_filament_type(s.filament_type);
            tray.tray_color   = s.color_hex;

            if (bundle) {
                tray.tray_info_idx = match_filament_preset(
                    bundle->filaments, s.vendor, s.brand_name, tray.tray_type);
            }

            trays.push_back(std::move(tray));
        }
    }

    build_ams_payload(box_count, max_slots - 1, trays);
    return true;
}

}
