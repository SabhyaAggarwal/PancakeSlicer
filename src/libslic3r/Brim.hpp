#ifndef slic3r_Brim_hpp_
#define slic3r_Brim_hpp_

#include "ExPolygon.hpp"
#include "ObjectID.hpp"
#include "Point.hpp"
#include "Polygon.hpp"

#include<map>
#include <utility>
#include<vector>

namespace Slic3r {

class Print;
class ExtrusionEntityCollection;
class PrintTryCancel;



void make_brim(Print& print, PrintTryCancel try_cancel,
    Polygons& islands_area, std::map<ObjectID, ExtrusionEntityCollection>& brimMap,
    std::map<ObjectInstanceID, ExtrusionEntityCollection>& brimMapByInstance,
    std::vector<std::pair<ObjectID, unsigned int>>& objPrintVec,
    std::vector<unsigned int>& printExtruders,
    std::map<ObjectInstanceID, ExPolygons>* objectBrimAreasByInstanceOut = nullptr);

ExtrusionEntityCollection makeBrimInfill(const ExPolygons& singleBrimArea, const Print& print, const Polygons& islands_area);
ExtrusionEntityCollection makeBrimInfillFromPlateCoordinates(const ExPolygons& singleBrimArea, const Print& print, const Polygons& islands_area);


ExtrusionEntityCollection make_brim_auto(const Print &print, PrintTryCancel try_cancel, Polygons &islands_area);

}

#endif
