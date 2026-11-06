#ifndef slic3r_SupportLayer_hpp_
#define slic3r_SupportLayer_hpp_

#include <memory>
#include <cstddef>
#include <oneapi/tbb/scalable_allocator.h>
#include <oneapi/tbb/spin_mutex.h>
#include <utility>
#include <vector>

#include "../libslic3r.h"
#include "../ClipperUtils.hpp"
#include "../Polygon.hpp"

namespace Slic3r {



enum class SupporLayerType {
	Unknown = 0,

	RaftBase,

	RaftInterface,

	BottomContact,


	BottomInterface,

	Base,


	TopInterface,

	TopContact,

	Intermediate,
};




class SupportGeneratorLayer
{
public:
	void reset() {
		*this = SupportGeneratorLayer();
	}

	bool operator==(const SupportGeneratorLayer &layer2) const {
		return print_z == layer2.print_z && height == layer2.height && bridging == layer2.bridging;
	}


	bool operator<(const SupportGeneratorLayer &layer2) const {
		if (print_z < layer2.print_z) {
			return true;
		} else if (print_z == layer2.print_z) {
            if (height > layer2.height)
                return true;
            else if (height == layer2.height) {

                return bridging && ! layer2.bridging;
            } else
                return false;
		} else
			return false;
	}

	void merge(SupportGeneratorLayer &&rhs) {

        this->polygons = union_(this->polygons, std::move(rhs.polygons));
        auto merge = [](std::unique_ptr<Polygons> &dst, std::unique_ptr<Polygons> &src) {
            if (! dst || dst->empty())
                dst = std::move(src);
            else if (src && ! src->empty())
                *dst = union_(*dst, std::move(*src));
        };
        merge(this->contact_polygons,  rhs.contact_polygons);
        merge(this->overhang_polygons, rhs.overhang_polygons);
        merge(this->enforcer_polygons, rhs.enforcer_polygons);
        rhs.reset();
    }



	coordf_t bottom_print_z() const { return print_z - height; }


	coordf_t extreme_z() const { return (this->layer_type == SupporLayerType::TopContact) ? this->bottom_z : this->print_z; }

	SupporLayerType layer_type { SupporLayerType::Unknown };

	coordf_t print_z { 0 };


	coordf_t bottom_z { 0 };

	coordf_t height { 0 };


	size_t 	 idx_object_layer_above { size_t(-1) };


	size_t 	 idx_object_layer_below { size_t(-1) };

	bool 	 bridging { false };


	Polygons polygons;

	std::unique_ptr<Polygons> contact_polygons;
	std::unique_ptr<Polygons> overhang_polygons;

	std::unique_ptr<Polygons> enforcer_polygons;
};




class SupportGeneratorLayerStorage {
public:
	SupportGeneratorLayer& allocate_unguarded(SupporLayerType layer_type) {
		m_storage.emplace_back();
		m_storage.back().layer_type = layer_type;
	    return m_storage.back();
	}

	SupportGeneratorLayer& allocate(SupporLayerType layer_type)
	{
		m_mutex.lock();
		m_storage.emplace_back();
	    SupportGeneratorLayer *layer_new = &m_storage.back();
		m_mutex.unlock();
	    layer_new->layer_type = layer_type;
	    return *layer_new;
	}

private:
	template<typename BaseType>
	using Allocator = tbb::scalable_allocator<BaseType>;
	Slic3r::deque<SupportGeneratorLayer, Allocator<SupportGeneratorLayer>> 		m_storage;
	tbb::spin_mutex                         									m_mutex;
};
using SupportGeneratorLayersPtr		= std::vector<SupportGeneratorLayer*>;

}

#endif
