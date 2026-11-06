#ifndef slic3r_EmbossShape_hpp_
#define slic3r_EmbossShape_hpp_

#include <string>
#include <optional>
#include <memory>
#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/archives/binary.hpp>
#include <vector>
#include "Point.hpp"
#include "ExPolygon.hpp"
#include "ExPolygonSerialize.hpp"
#include "libslic3r.h"
#include "nanosvg/nanosvg.h"

namespace Slic3r {

struct EmbossProjection{

    double depth = 1.;



    bool use_surface = false;

    bool operator==(const EmbossProjection &other) const {
        return depth == other.depth && use_surface == other.use_surface;
    }


    template<class Archive> void serialize(Archive &ar) { ar(depth, use_surface); }
};


struct HealedExPolygons{
    ExPolygons expolygons;
    bool is_healed;
    operator ExPolygons&() { return expolygons; }
};



struct ExPolygonsWithId
{



    unsigned id;



    ExPolygons expoly;


    bool is_healed = true;
};
using ExPolygonsWithIds = std::vector<ExPolygonsWithId>;




struct EmbossShape
{

    ExPolygonsWithIds shapes_with_ids;





    HealedExPolygons final_shape;


    double scale = SCALING_FACTOR;


    EmbossProjection projection;






    std::optional<Slic3r::Transform3d> fix_3mf_tr;

    struct SvgFile {


        std::string path;




        std::string path_in_3mf;



        std::shared_ptr<NSVGimage> image = nullptr;


        std::shared_ptr<std::string> file_data = nullptr;

        template<class Archive> void save(Archive &ar) const {




            ar(path, path_in_3mf, (file_data != nullptr) ? *file_data : std::string(""));
        }
        template<class Archive> void load(Archive &ar) {

            std::string file_data_str;
            ar(path, path_in_3mf, file_data_str);
            if (!file_data_str.empty())
                file_data = std::make_unique<std::string>(file_data_str);
        }
    };

    std::optional<SvgFile> svg_file;


    template<class Archive> void save(Archive &ar) const
    {

        ar(shapes_with_ids, final_shape, scale, projection, svg_file);
        cereal::save(ar, fix_3mf_tr);
    }
    template<class Archive> void load(Archive &ar)
    {
        ar(shapes_with_ids, final_shape, scale, projection, svg_file);
        cereal::load(ar, fix_3mf_tr);
    }
};
}


namespace cereal {
template<class Archive> void serialize(Archive &ar, Slic3r::ExPolygonsWithId &o) { ar(o.id, o.expoly, o.is_healed); }
template<class Archive> void serialize(Archive &ar, Slic3r::HealedExPolygons &o) { ar(o.expolygons, o.is_healed); }
};

#endif
