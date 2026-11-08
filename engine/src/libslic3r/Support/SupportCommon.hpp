#ifndef slic3r_SupportCommon_hpp_
#define slic3r_SupportCommon_hpp_

#include "../Polygon.hpp"
#include "../Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "SupportLayer.hpp"
#include "SupportParameters.hpp"
#include "../Point.hpp"
#include "../libslic3r.h"
#include <utility>
#include <algorithm>
#include <vector>

namespace Slic3r { class Fill; }
namespace Slic3r { class Flow; }
namespace Slic3r { struct SlicingParameters; }

namespace Slic3r {

class PrintObject;
class SupportLayer;

std::pair<SupportGeneratorLayersPtr, SupportGeneratorLayersPtr> generate_interface_layers(
    const PrintObjectConfig           &config,
    const SupportParameters           &support_params,
    const SupportGeneratorLayersPtr   &bottom_contacts,
    const SupportGeneratorLayersPtr   &top_contacts,
    SupportGeneratorLayersPtr         &top_interface_layers,
    SupportGeneratorLayersPtr         &top_base_interface_layers,
    SupportGeneratorLayersPtr         &intermediate_layers,
    SupportGeneratorLayerStorage      &layer_storage);

SupportGeneratorLayersPtr generate_raft_base(
	const PrintObject				&object,
	const SupportParameters			&support_params,
	const SlicingParameters			&slicing_params,
	const SupportGeneratorLayersPtr &top_contacts,
	const SupportGeneratorLayersPtr &interface_layers,
	const SupportGeneratorLayersPtr &base_interface_layers,
	const SupportGeneratorLayersPtr &base_layers,
	SupportGeneratorLayerStorage    &layer_storage);

void tree_supports_generate_paths(ExtrusionEntitiesPtr &dst, const Polygons &polygons, const Flow &flow, const SupportParameters &support_params);

void fill_expolygons_with_sheath_generate_paths(
    ExtrusionEntitiesPtr &dst, const Polygons &polygons, Fill *filler, float density, ExtrusionRole role, const Flow &flow, const SupportParameters& support_params, bool with_sheath, bool no_sort);

SupportGeneratorLayersPtr generate_support_layers(
	PrintObject							&object,
    const SupportGeneratorLayersPtr     &raft_layers,
    const SupportGeneratorLayersPtr     &bottom_contacts,
    const SupportGeneratorLayersPtr     &top_contacts,
    const SupportGeneratorLayersPtr     &intermediate_layers,
    const SupportGeneratorLayersPtr     &interface_layers,
    const SupportGeneratorLayersPtr     &base_interface_layers);

void generate_support_toolpaths(
	SupportLayerPtrs    				&support_layers,
	const PrintObjectConfig 			&config,
	const SupportParameters 			&support_params,
	const SlicingParameters 			&slicing_params,
    const SupportGeneratorLayersPtr 	&raft_layers,
    const SupportGeneratorLayersPtr   	&bottom_contacts,
    const SupportGeneratorLayersPtr   	&top_contacts,
    const SupportGeneratorLayersPtr   	&intermediate_layers,
	const SupportGeneratorLayersPtr   	&interface_layers,
    const SupportGeneratorLayersPtr   	&base_interface_layers);

template<typename IteratorType, typename IndexType, typename FN_HIGHER_EQUAL>
IndexType idx_higher_or_equal(IteratorType begin, IteratorType end, IndexType idx, FN_HIGHER_EQUAL fn_higher_equal)
{
    auto size = int(end - begin);
    if (size == 0) {
        idx = 0;
    } else if (idx == IndexType(-1)) {
        int idx_low  = 0;
        int idx_high = std::max(0, size - 1);
        while (idx_low + 1 < idx_high) {
            int idx_mid  = (idx_low + idx_high) / 2;
            if (fn_higher_equal(begin[idx_mid]))
                idx_high = idx_mid;
            else
                idx_low  = idx_mid;
        }
        idx =  fn_higher_equal(begin[idx_low])  ? idx_low  :
              (fn_higher_equal(begin[idx_high]) ? idx_high : size);
    } else {
        while (int(idx) < size && ! fn_higher_equal(begin[idx]))
            ++ idx;
    }
    return idx;
}
template<typename T, typename IndexType, typename FN_HIGHER_EQUAL>
IndexType idx_higher_or_equal(const std::vector<T>& vec, IndexType idx, FN_HIGHER_EQUAL fn_higher_equal)
{
    return idx_higher_or_equal(vec.begin(), vec.end(), idx, fn_higher_equal);
}

template<typename IT, typename FN_LOWER_EQUAL>
int idx_lower_or_equal(IT begin, IT end, int idx, FN_LOWER_EQUAL fn_lower_equal)
{
    auto size = int(end - begin);
    if (size == 0) {
        idx = -1;
    } else if (idx < -1) {
        int idx_low  = 0;
        int idx_high = std::max(0, size - 1);
        while (idx_low + 1 < idx_high) {
            int idx_mid  = (idx_low + idx_high) / 2;
            if (fn_lower_equal(begin[idx_mid]))
                idx_low  = idx_mid;
            else
                idx_high = idx_mid;
        }
        idx =  fn_lower_equal(begin[idx_high]) ? idx_high :
              (fn_lower_equal(begin[idx_low ]) ? idx_low  : -1);
    } else {
        while (idx >= 0 && ! fn_lower_equal(begin[idx]))
            -- idx;
    }
    return idx;
}
template<typename T, typename FN_LOWER_EQUAL>
int idx_lower_or_equal(const std::vector<T*> &vec, int idx, FN_LOWER_EQUAL fn_lower_equal)
{
    return idx_lower_or_equal(vec.begin(), vec.end(), idx, fn_lower_equal);
}

Polygons belt_floor_surface_polygon(
    const SlicingParameters &slicing_params, const PrintConfig &print_config,
    const PrintObject &object, coordf_t print_z);

Vec2d build_plate_tilt_slope(const PrintConfig &print_config);

}

#endif
