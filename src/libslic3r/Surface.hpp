#ifndef slic3r_Surface_hpp_
#define slic3r_Surface_hpp_

#include "Polygon.hpp"
#include "Point.hpp"
#include "ExPolygon.hpp"
#include <utility>
#include <vector>
#include <cstddef>
#include <algorithm>
#include <iterator>
#include "BoundingBox.hpp"

namespace Slic3r {

enum SurfaceType {

    stTop,

    stBottom,

    stBottomBridge,

    stInternalAfterExternalBridge,

    stInternal,

    stInternalSolid,

    stInternalBridge,

    stSecondInternalBridge,


    stInternalVoid,

    stPerimeter,

    stTopNonplanar,
    stInternalSolidNonplanar,

    stCount,
};

class Surface
{
public:
    SurfaceType     surface_type;
    ExPolygon       expolygon;
    double          thickness;
    unsigned short  thickness_layers;
    double          bridge_angle;
    unsigned short  extra_perimeters;



    float           distance_to_top   { -1 };
    size_t          source_layer_id   { size_t(-1) };
    int             source_surface_id { -1 };



    bool same_nonplanar_shell(const Surface &rhs) const {
        return this->is_nonplanar() == rhs.is_nonplanar() &&
            (! this->is_nonplanar() ||
             (this->source_surface_id == rhs.source_surface_id &&
              std::abs(this->distance_to_top - rhs.distance_to_top) < 1e-4f));
    }

    Surface(SurfaceType _surface_type = stInternal)
        : surface_type(_surface_type),
            thickness(-1), thickness_layers(1), bridge_angle(-1), extra_perimeters(0)
        {};
    Surface(const Slic3r::Surface &rhs)
        : surface_type(rhs.surface_type), expolygon(rhs.expolygon),
            thickness(rhs.thickness), thickness_layers(rhs.thickness_layers),
            bridge_angle(rhs.bridge_angle), extra_perimeters(rhs.extra_perimeters),
            distance_to_top(rhs.distance_to_top), source_layer_id(rhs.source_layer_id), source_surface_id(rhs.source_surface_id)
        {};

    Surface(SurfaceType _surface_type, const ExPolygon &_expolygon)
        : surface_type(_surface_type), expolygon(_expolygon),
            thickness(-1), thickness_layers(1), bridge_angle(-1), extra_perimeters(0)
        {};
    Surface(const Surface &other, const ExPolygon &_expolygon)
        : surface_type(other.surface_type), expolygon(_expolygon),
            thickness(other.thickness), thickness_layers(other.thickness_layers),
            bridge_angle(other.bridge_angle), extra_perimeters(other.extra_perimeters),
            distance_to_top(other.distance_to_top), source_layer_id(other.source_layer_id), source_surface_id(other.source_surface_id)
        {};
    Surface(Surface &&rhs) noexcept
        : surface_type(rhs.surface_type), expolygon(std::move(rhs.expolygon)),
            thickness(rhs.thickness), thickness_layers(rhs.thickness_layers),
            bridge_angle(rhs.bridge_angle), extra_perimeters(rhs.extra_perimeters),
            distance_to_top(rhs.distance_to_top), source_layer_id(rhs.source_layer_id), source_surface_id(rhs.source_surface_id)
        {};
    Surface(SurfaceType _surface_type, const ExPolygon &&_expolygon)
        : surface_type(_surface_type), expolygon(std::move(_expolygon)),
            thickness(-1), thickness_layers(1), bridge_angle(-1), extra_perimeters(0)
        {};
    Surface(const Surface &other, const ExPolygon &&_expolygon)
        : surface_type(other.surface_type), expolygon(std::move(_expolygon)),
            thickness(other.thickness), thickness_layers(other.thickness_layers),
            bridge_angle(other.bridge_angle), extra_perimeters(other.extra_perimeters),
            distance_to_top(other.distance_to_top), source_layer_id(other.source_layer_id), source_surface_id(other.source_surface_id)
        {};

    Surface& operator=(const Surface &rhs)
    {
        surface_type     = rhs.surface_type;
        expolygon        = rhs.expolygon;
        thickness        = rhs.thickness;
        thickness_layers = rhs.thickness_layers;
        bridge_angle     = rhs.bridge_angle;
        extra_perimeters = rhs.extra_perimeters;
        distance_to_top  = rhs.distance_to_top;
        source_layer_id  = rhs.source_layer_id;
        source_surface_id = rhs.source_surface_id;
        return *this;
    }

    Surface& operator=(Surface &&rhs) noexcept
    {
        surface_type     = rhs.surface_type;
        expolygon        = std::move(rhs.expolygon);
        thickness        = rhs.thickness;
        thickness_layers = rhs.thickness_layers;
        bridge_angle     = rhs.bridge_angle;
        extra_perimeters = rhs.extra_perimeters;
        distance_to_top  = rhs.distance_to_top;
        source_layer_id  = rhs.source_layer_id;
        source_surface_id = rhs.source_surface_id;
        return *this;
    }

	double area() 		 const { return this->expolygon.area(); }
    bool   empty() 		 const { return expolygon.empty(); }
    void   clear() 			   { expolygon.clear(); }


	bool   is_top()      const { return this->surface_type == stTop || this->surface_type == stTopNonplanar; }
	bool   is_bottom()   const { return this->surface_type == stBottom || this->surface_type == stBottomBridge; }
	bool   is_bridge()   const { return this->surface_type == stBottomBridge || this->surface_type == stInternalBridge; }
    bool   is_internal_bridge() const { return this->surface_type == stInternalBridge; }
	bool   is_external() const { return this->is_top() || this->is_bottom(); }
	bool   is_internal() const { return ! this->is_external(); }
	bool   is_solid()    const { return this->is_external() || this->surface_type == stInternalSolid || this->surface_type == stInternalSolidNonplanar || this->surface_type == stInternalBridge; }
	bool   is_solid_infill() const { return this->surface_type == stInternalSolid; }
	bool   is_internal_solid() const { return this->surface_type == stInternalSolid || this->surface_type == stInternalSolidNonplanar; }
	bool   is_nonplanar() const { return this->surface_type == stTopNonplanar || this->surface_type == stInternalSolidNonplanar; }
};

typedef std::vector<Surface> Surfaces;
typedef std::vector<const Surface*> SurfacesPtr;

inline Polygons to_polygons(const Surface &surface)
{
    return to_polygons(surface.expolygon);
}

inline Polygons to_polygons(Surface &&surface)
{
    return to_polygons(std::move(surface.expolygon));
}

inline Polygons to_polygons(const Surfaces &src)
{
    size_t num = 0;
    for (Surfaces::const_iterator it = src.begin(); it != src.end(); ++it)
        num += it->expolygon.holes.size() + 1;
    Polygons polygons;
    polygons.reserve(num);
    for (Surfaces::const_iterator it = src.begin(); it != src.end(); ++it) {
        polygons.emplace_back(it->expolygon.contour);
        for (Polygons::const_iterator ith = it->expolygon.holes.begin(); ith != it->expolygon.holes.end(); ++ith)
            polygons.emplace_back(*ith);
    }
    return polygons;
}

inline Polygons to_polygons(const SurfacesPtr &src)
{
    size_t num = 0;
    for (SurfacesPtr::const_iterator it = src.begin(); it != src.end(); ++it)
        num += (*it)->expolygon.holes.size() + 1;
    Polygons polygons;
    polygons.reserve(num);
    for (SurfacesPtr::const_iterator it = src.begin(); it != src.end(); ++it) {
        polygons.emplace_back((*it)->expolygon.contour);
        for (Polygons::const_iterator ith = (*it)->expolygon.holes.begin(); ith != (*it)->expolygon.holes.end(); ++ith)
            polygons.emplace_back(*ith);
    }
    return polygons;
}

inline ExPolygons to_expolygons(const Surfaces &src)
{
    ExPolygons expolygons;
    expolygons.reserve(src.size());
    for (Surfaces::const_iterator it = src.begin(); it != src.end(); ++it)
        expolygons.emplace_back(it->expolygon);
    return expolygons;
}

inline ExPolygons to_expolygons(Surfaces &&src)
{
	ExPolygons expolygons;
	expolygons.reserve(src.size());
	for (Surfaces::const_iterator it = src.begin(); it != src.end(); ++it)
		expolygons.emplace_back(ExPolygon(std::move(it->expolygon)));
	src.clear();
	return expolygons;
}

inline ExPolygons to_expolygons(const SurfacesPtr &src)
{
    ExPolygons expolygons;
    expolygons.reserve(src.size());
    for (SurfacesPtr::const_iterator it = src.begin(); it != src.end(); ++it)
        expolygons.emplace_back((*it)->expolygon);
    return expolygons;
}



inline size_t number_polygons(const Surfaces &surfaces)
{
    size_t n_polygons = 0;
    for (Surfaces::const_iterator it = surfaces.begin(); it != surfaces.end(); ++ it)
        n_polygons += it->expolygon.holes.size() + 1;
    return n_polygons;
}
inline size_t number_polygons(const SurfacesPtr &surfaces)
{
    size_t n_polygons = 0;
    for (SurfacesPtr::const_iterator it = surfaces.begin(); it != surfaces.end(); ++ it)
        n_polygons += (*it)->expolygon.holes.size() + 1;
    return n_polygons;
}


inline void polygons_append(Polygons &dst, const Surfaces &src)
{
    dst.reserve(dst.size() + number_polygons(src));
    for (Surfaces::const_iterator it = src.begin(); it != src.end(); ++ it) {
        dst.emplace_back(it->expolygon.contour);
        dst.insert(dst.end(), it->expolygon.holes.begin(), it->expolygon.holes.end());
    }
}

inline void polygons_append(Polygons &dst, Surfaces &&src)
{
    dst.reserve(dst.size() + number_polygons(src));
    for (Surfaces::iterator it = src.begin(); it != src.end(); ++ it) {
        dst.emplace_back(std::move(it->expolygon.contour));
        std::move(std::begin(it->expolygon.holes), std::end(it->expolygon.holes), std::back_inserter(dst));
        it->expolygon.holes.clear();
    }
}


inline void polygons_append(Polygons &dst, const SurfacesPtr &src)
{
    dst.reserve(dst.size() + number_polygons(src));
    for (SurfacesPtr::const_iterator it = src.begin(); it != src.end(); ++ it) {
        dst.emplace_back((*it)->expolygon.contour);
        dst.insert(dst.end(), (*it)->expolygon.holes.begin(), (*it)->expolygon.holes.end());
    }
}














inline void surfaces_append(Surfaces &dst, const ExPolygons &src, SurfaceType surfaceType)
{
    dst.reserve(dst.size() + src.size());
    for (const ExPolygon &expoly : src)
        dst.emplace_back(Surface(surfaceType, expoly));
}
inline void surfaces_append(Surfaces &dst, const ExPolygons &src, SurfaceType surfaceType, float distance_to_top)
{
    dst.reserve(dst.size() + src.size());
    for (const ExPolygon &expoly : src) {
        Surface s = Surface(surfaceType, expoly);
        s.distance_to_top = distance_to_top;
        dst.emplace_back(s);
    }
}
inline void surfaces_append(Surfaces &dst, const ExPolygons &src, const Surface &surfaceTempl)
{
    dst.reserve(dst.size() + number_polygons(src));
    for (const ExPolygon &expoly : src)
        dst.emplace_back(Surface(surfaceTempl, expoly));
}
inline void surfaces_append(Surfaces &dst, const Surfaces &src)
{
    dst.insert(dst.end(), src.begin(), src.end());
}

inline void surfaces_append(Surfaces &dst, ExPolygons &&src, SurfaceType surfaceType)
{
    dst.reserve(dst.size() + src.size());
    for (ExPolygon &expoly : src)
        dst.emplace_back(Surface(surfaceType, std::move(expoly)));
    src.clear();
}

inline void surfaces_append(Surfaces &dst, ExPolygons &&src, const Surface &surfaceTempl)
{
    dst.reserve(dst.size() + number_polygons(src));
    for (ExPolygons::const_iterator it = src.begin(); it != src.end(); ++ it)
        dst.emplace_back(Surface(surfaceTempl, std::move(*it)));
    src.clear();
}

inline void surfaces_append(Surfaces &dst, Surfaces &&src)
{
    if (dst.empty()) {
        dst = std::move(src);
    } else {
        std::move(std::begin(src), std::end(src), std::back_inserter(dst));
        src.clear();
    }
}

extern BoundingBox get_extents(const Surface &surface);
extern BoundingBox get_extents(const Surfaces &surfaces);
extern BoundingBox get_extents(const SurfacesPtr &surfaces);

inline bool surfaces_could_merge(const Surface &s1, const Surface &s2)
{
    return
        s1.surface_type      == s2.surface_type     &&
        s1.thickness         == s2.thickness        &&
        s1.thickness_layers  == s2.thickness_layers &&
        s1.bridge_angle      == s2.bridge_angle     &&
        s1.distance_to_top   == s2.distance_to_top;
}

class SVG;

extern const char* surface_type_to_color_name(const SurfaceType surface_type);
extern void export_surface_type_legend_to_svg(SVG &svg, const Point &pos);
extern Point export_surface_type_legend_to_svg_box_size();
extern bool export_to_svg(const char *path, const Surfaces &surfaces, const float transparency = 1.f);

}

#endif
