#ifndef __CREALITY_PRINT_AGENT_HPP__
#define __CREALITY_PRINT_AGENT_HPP__

#include "IPrinterAgent.hpp"
#include "MoonrakerPrinterAgent.hpp"

#include <string>
#include <vector>

namespace Slic3r {

class PresetCollection;

class CrealityPrintAgent final : public MoonrakerPrinterAgent
{
public:
    struct CFSSlot
    {
        int         box_id  = 0;
        int         slot_id = 0;
        std::string color_hex;
        std::string filament_type;
        std::string brand_name;
        std::string vendor;
    };

    explicit CrealityPrintAgent(std::string log_dir);
    ~CrealityPrintAgent() override = default;

    static AgentInfo get_agent_info_static();
    AgentInfo        get_agent_info() override { return get_agent_info_static(); }

    bool fetch_filament_info(std::string dev_id, FilamentSyncMode sync_mode = FilamentSyncMode::pull) override;

    static bool parse_cfs_response(const std::string&    response,
                                   std::vector<CFSSlot>& slots,
                                   int&                  box_count,
                                   std::string&          error);

    static std::string normalize_filament_type(const std::string& filament_type);

    static std::string match_filament_preset(const PresetCollection& filaments,
                                             const std::string&      vendor,
                                             const std::string&      brand_name,
                                             const std::string&      base_type);
};

}

#endif
