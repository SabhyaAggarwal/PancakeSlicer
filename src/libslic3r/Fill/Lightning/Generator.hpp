//Copyright (c) 2021 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef LIGHTNING_GENERATOR_H
#define LIGHTNING_GENERATOR_H

#include "libslic3r/BoundingBox.hpp"
#include "Layer.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/libslic3r.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace Slic3r
{
class PrintObject;

namespace FillLightning
{


















class Generator
{
public:







    explicit Generator(const PrintObject &print_object, const std::function<void()> &throw_on_cancel_callback);










    const Layer& getTreesForLayer(const size_t& layer_id) const;

    std::vector<Polygons>& Overhangs() { return m_overhang_per_layer; }

    float infilll_extrusion_width() const { return m_infill_extrusion_width; }

    Generator(PrintObject* m_object, std::vector<Polygons>& contours, std::vector<Polygons>& overhangs, const std::function<void()> &throw_on_cancel_callback, float density = 0.15);

protected:








    void generateInitialInternalOverhangs(const PrintObject &print_object, const std::function<void()> &throw_on_cancel_callback);




    void generateTrees(const PrintObject &print_object, const std::function<void()> &throw_on_cancel_callback);
    void generateTreesforSupport(std::vector<Polygons>& contours, const std::function<void()> &throw_on_cancel_callback);

    float m_infill_extrusion_width;




    coord_t m_supporting_radius;








    coord_t m_wall_supporting_radius;








    coord_t m_prune_length;









    coord_t m_straightening_max_distance;






    std::vector<Polygons> m_overhang_per_layer;






    std::vector<Layer> m_lightning_layers;

    std::vector<BoundingBox> bboxs;
};

}
}

#endif
