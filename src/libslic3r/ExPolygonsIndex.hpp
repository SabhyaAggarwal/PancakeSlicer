#ifndef slic3r_ExPolygonsIndex_hpp_
#define slic3r_ExPolygonsIndex_hpp_

#include "ExPolygon.hpp"
#include <cstdint>
#include <vector>
namespace Slic3r {





struct ExPolygonsIndex
{

    uint32_t expolygons_index;




    uint32_t polygon_index;


    uint32_t point_index;

    bool is_contour() const { return polygon_index == 0; }
    bool is_hole() const { return polygon_index != 0; }
    uint32_t hole_index() const { return polygon_index - 1; }
};









class ExPolygonsIndices
{
    std::vector<std::vector<uint32_t>> m_offsets;

    uint32_t m_count;
public:
    ExPolygonsIndices(const ExPolygons &shapes);






    uint32_t cvt(const ExPolygonsIndex &id) const;






    ExPolygonsIndex cvt(uint32_t index) const;






    bool is_last_point(const ExPolygonsIndex &id) const;





    uint32_t get_count() const;
};

}
#endif
