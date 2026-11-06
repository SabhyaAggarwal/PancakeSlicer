#ifndef slic3r_ThumbnailData_hpp_
#define slic3r_ThumbnailData_hpp_

#include <functional>
#include <string>
#include <vector>
#include "libslic3r/libslic3r.h"
#include "libslic3r/Point.hpp"
#include "nlohmann/json.hpp"

struct indexed_triangle_set;

namespace Slic3r {


static std::vector<Vec2d> THUMBNAIL_SIZE = { Vec2d(50, 50) };

struct ThumbnailData
{
    unsigned int width;
    unsigned int height;
    std::vector<unsigned char> pixels;

    ThumbnailData() { reset(); }
    void set(unsigned int w, unsigned int h);
    void reset();

    bool is_valid() const;
    void load_from(ThumbnailData &data) {
        this->set(data.width, data.height);
        pixels = data.pixels;
    }
};


ThumbnailData render_thumbnail(const indexed_triangle_set &mesh, unsigned width, unsigned height, bool top, const std::string &colour = "#00AE42");


using ThumbnailsList = std::vector<ThumbnailData>;

struct ThumbnailsParams
{
	const Vec2ds 	sizes{};
	bool 			printable_only;
	bool 			parts_only;
	bool 			show_bed;
	bool 			transparent_background;
    int             plate_id;
    bool            use_plate_box{true};
};

typedef std::function<ThumbnailsList(const ThumbnailsParams&)> ThumbnailsGeneratorCallback;

struct BBoxData
{
    int id;
    std::vector<coordf_t> bbox;
    float area;
    float layer_height;
    std::string name;
    void to_json(nlohmann::json& j) const{
        j = nlohmann::json{
            {"id",id},
            {"bbox",bbox},
            {"area",area},
            {"layer_height",layer_height},
            {"name",name}
        };
    }
    void from_json(const nlohmann::json& j) {
        j.at("id").get_to(id);
        j.at("bbox").get_to(bbox);
        j.at("area").get_to(area);
        j.at("layer_height").get_to(layer_height);
        j.at("name").get_to(name);
    }
};

struct PlateBBoxData
{
    std::vector<coordf_t> bbox_all;
    std::vector<BBoxData> bbox_objs;
    std::vector<int>      filament_ids;
    std::vector<std::string> filament_colors;
    bool is_seq_print = false;
    int first_extruder = 0;
    float nozzle_diameter = 0.4;
    std::string bed_type;
    float first_layer_time = 0.0f;


    int version = 2;

    void to_json(nlohmann::json& j) const{
        j = nlohmann::json{ {"bbox_all",bbox_all} };
        j["filament_ids"] = filament_ids;
        j["filament_colors"] = filament_colors;
        j["is_seq_print"]    = is_seq_print;
        j["first_extruder"] = first_extruder;
        j["nozzle_diameter"] = nozzle_diameter;
        j["version"]         = version;
        j["bed_type"]        = bed_type;
        j["first_layer_time"] = first_layer_time;
        for (const auto& bbox : bbox_objs) {
            nlohmann::json j_bbox;
            bbox.to_json(j_bbox);
            j["bbox_objects"].push_back(j_bbox);
        }
    }
    void from_json(const nlohmann::json& j) {
        j.at("bbox_all").get_to(bbox_all);
        j.at("filament_ids").get_to(filament_ids);
        j.at("filament_colors").get_to(filament_colors);
        j.at("is_seq_print").get_to(is_seq_print);
        j.at("first_extruder").get_to(first_extruder);
        j.at("nozzle_diameter").get_to(nozzle_diameter);
        j.at("version").get_to(version);
        j.at("bed_type").get_to(bed_type);
        for (auto& bbox_j : j.at("bbox_objects")) {
            BBoxData bbox_data;
            bbox_data.from_json(bbox_j);
            bbox_objs.push_back(bbox_data);
        }
    }
    bool is_valid() const {
        return !bbox_objs.empty();
    }
};

}

#endif
