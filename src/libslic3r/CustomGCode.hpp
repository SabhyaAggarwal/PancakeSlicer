#ifndef slic3r_CustomGCode_hpp_
#define slic3r_CustomGCode_hpp_

#include <map>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

namespace Slic3r {

class DynamicPrintConfig;

namespace CustomGCode {

enum Type
{
    ColorChange,
    PausePrint,
    ToolChange,
    Template,
    Custom,
    Unknown,
};

struct Item
{
    bool operator<(const Item& rhs) const { return this->print_z < rhs.print_z; }
    bool operator==(const Item& rhs) const
    {
        return (rhs.print_z   == this->print_z    ) &&
               (rhs.type      == this->type       ) &&
               (rhs.extruder  == this->extruder   ) &&
               (rhs.color     == this->color      ) &&
               (rhs.extra     == this->extra      );
    }
    bool operator!=(const Item& rhs) const { return ! (*this == rhs); }
    
    double      print_z;
    Type        type;
    int         extruder;
    std::string color;
    std::string extra;
    void from_json(const nlohmann::json& j) {
        std::string type_str;
        j.at("type").get_to(type_str);
        std::map<std::string,Type> str2type = { {"ColorChange", ColorChange },
            {"PausePrint",PausePrint},
            {"ToolChange",ToolChange},
            {"Template",Template},
            {"Custom",Custom},
            {"Unknown",Unknown} };
        type = Unknown;
        if (str2type.find(type_str) != str2type.end())
            type = str2type[type_str];
        j.at("print_z").get_to(print_z);
        j.at("color").get_to(color);
        j.at("extruder").get_to(extruder);
        if(j.contains("extra"))
            j.at("extra").get_to(extra);
    }
};

enum Mode
{
    Undef,
    SingleExtruder,
    MultiAsSingle,
    MultiExtruder
};

static constexpr char SingleExtruderMode[] = "SingleExtruder";
static constexpr char MultiAsSingleMode [] = "MultiAsSingle";
static constexpr char MultiExtruderMode [] = "MultiExtruder";

struct Info
{
    Mode mode = Undef;
    std::vector<Item> gcodes;

    bool operator==(const Info& rhs) const
    {
        return  (rhs.mode   == this->mode   ) &&
                (rhs.gcodes == this->gcodes );
    }
    bool operator!=(const Info& rhs) const { return !(*this == rhs); }

    void from_json(const nlohmann::json& j) {
        std::string mode_str;
        if (j.contains("mode"))
            j.at("mode").get_to(mode_str);
        if (mode_str == "SingleExtruder") mode = SingleExtruder;
        else if (mode_str == "MultiAsSingle") mode = MultiAsSingle;
        else if (mode_str == "MultiExtruder") mode = MultiExtruder;
        else mode = Undef;

        auto j_gcodes = j.at("gcodes");
        gcodes.reserve(j_gcodes.size());
        for (auto& jj : j_gcodes) {
            Item item;
            item.from_json(jj);
            gcodes.push_back(item);
        }
    }
};

extern void check_mode_for_custom_gcode_per_print_z(Info& info);

std::vector<std::pair<double, unsigned int>> custom_tool_changes(const Info& custom_gcode_per_print_z, size_t num_extruders);

}

}



#endif
