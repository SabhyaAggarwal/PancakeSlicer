


#ifndef slic3r_PrintExtents_hpp_
#define slic3r_PrintExtents_hpp_

#include "libslic3r.h"

namespace Slic3r {

class Print;
class PrintObject;
class BoundingBoxf;


BoundingBoxf get_print_extrusions_extents(const Print &print);


BoundingBoxf get_print_object_extrusions_extents(const PrintObject &print_object, const coordf_t max_print_z);



BoundingBoxf get_wipe_tower_extrusions_extents(const Print &print, const coordf_t max_print_z);


BoundingBoxf get_wipe_tower_priming_extrusions_extents(const Print &print);

};

#endif
