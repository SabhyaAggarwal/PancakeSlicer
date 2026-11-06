#ifndef slic3r_SupportMaterial_hpp_
#define slic3r_SupportMaterial_hpp_

#include "libslic3r/Polygon.hpp"
#include "PrintConfig.hpp"
#include "Slicing.hpp"
#include "SupportLayer.hpp"
#include "SupportParameters.hpp"
#include <vector>
#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
namespace Slic3r {

class PrintObject;
class PrintConfig;
class PrintObjectConfig;





class PrintObjectSupportMaterial
{
public:
	PrintObjectSupportMaterial(const PrintObject *object, const SlicingParameters &slicing_params);


	bool 		has_raft() 					const { return m_slicing_params.has_raft(); }

	bool 		has_support()				const { return m_object_config->enable_support.value || m_object_config->enforce_support_layers; }
	bool 		build_plate_only() 			const { return this->has_support() && m_object_config->support_on_build_plate_only.value; }

	bool 		synchronize_layers()		const { return  !m_print_config->independent_support_layer_height.value; }
	bool 		has_contact_loops() 		const { return m_object_config->support_interface_loop_pattern.value; }




	void 		generate(PrintObject &object);

private:
	std::vector<Polygons> buildplate_covered(const PrintObject &object) const;




	SupportGeneratorLayersPtr top_contact_layers(const PrintObject &object, const std::vector<Polygons> &buildplate_covered, SupportGeneratorLayerStorage &layer_storage) const;




	SupportGeneratorLayersPtr bottom_contact_layers_and_layer_support_areas(
		const PrintObject &object, const SupportGeneratorLayersPtr &top_contacts, std::vector<Polygons> &buildplate_covered,
		SupportGeneratorLayerStorage &layer_storage, std::vector<Polygons> &layer_support_areas) const;


	void trim_top_contacts_by_bottom_contacts(const PrintObject &object, const SupportGeneratorLayersPtr &bottom_contacts, SupportGeneratorLayersPtr &top_contacts) const;


	SupportGeneratorLayersPtr raft_and_intermediate_support_layers(
	    const PrintObject   &object,
	    const SupportGeneratorLayersPtr   &bottom_contacts,
	    const SupportGeneratorLayersPtr   &top_contacts,
	    SupportGeneratorLayerStorage	  &layer_storage) const;


	void generate_base_layers(
	    const PrintObject   &object,
	    const SupportGeneratorLayersPtr   &bottom_contacts,
	    const SupportGeneratorLayersPtr   &top_contacts,
	    SupportGeneratorLayersPtr         &intermediate_layers,
	    const std::vector<Polygons> &layer_support_areas) const;





	void trim_support_layers_by_object(
	    const PrintObject   &object,
	    SupportGeneratorLayersPtr         &support_layers,
	    const coordf_t       gap_extra_above,
	    const coordf_t       gap_extra_below,
	    const coordf_t       gap_xy) const;







	const PrintConfig 		*m_print_config;
	const PrintObjectConfig *m_object_config;


	SlicingParameters	     m_slicing_params;

	SupportParameters   	 m_support_params;
};

}

#endif
