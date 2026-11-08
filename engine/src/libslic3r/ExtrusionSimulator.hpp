#ifndef slic3r_ExtrusionSimulator_hpp_
#define slic3r_ExtrusionSimulator_hpp_

#include "Point.hpp"
#include "ExtrusionEntity.hpp"
#include "BoundingBox.hpp"

namespace Slic3r {

enum ExtrusionSimulationType
{
    ExtrusionSimulationSimple,
    ExtrusionSimulationDontSpread,
    ExtrisopmSimulationSpreadNotOverfilled,
    ExtrusionSimulationSpreadFull,
    ExtrusionSimulationSpreadExcess
};

class ExtrusionSimulatorImpl;

class ExtrusionSimulator
{
public:
    ExtrusionSimulator();
    ~ExtrusionSimulator();

    void  		set_image_size(const Point &image_size);
    void  		set_viewport(const BoundingBox &viewport);
    void		set_bounding_box(const BoundingBox &bbox);

    void		reset_accumulator();
    void		extrude_to_accumulator(const ExtrusionPath &path, const Point &shift, ExtrusionSimulationType simulationType);
    void		evaluate_accumulator(ExtrusionSimulationType simulationType);
    const void* image_ptr() const;

private:
	Point					    	image_size;
	BoundingBox				    	viewport;
	BoundingBox 					bbox;

	ExtrusionSimulatorImpl		   *pimpl;
};

}

#endif
