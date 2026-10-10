#ifndef WipeTower2_
#define WipeTower2_

#include <cmath>
#include <cstddef>
#include <limits>
#include <math.h>
#include <string>
#include <sstream>
#include <utility>
#include <algorithm>
#include <vector>

#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "WipeTower.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/PrintConfig.hpp"
namespace Slic3r
{

class WipeTowerWriter2;
class PrintRegionConfig;
class ConfigBase;

class WipeTower2
{
public:
    static const std::string never_skip_tag() { return "_GCODE_WIPE_TOWER_NEVER_SKIP_TAG"; }
    static const std::string wait_for_temp_tag() { return ";_WAIT_FOR_TEMP_ON_WIPE_TOWER"; }
	static std::pair<double, double> get_wipe_tower_cone_base(double width, double height, double depth, double angle_deg);
	static Polygon cone_base_polygon(double width, double depth, double height, double angle_deg);
	static std::vector<std::vector<float>> extract_wipe_volumes(const ConfigBase& config);
	static float estimate_semm_flush_volume(const ConfigBase& config, size_t filaments_cnt);

    
    WipeTower::ToolChangeResult construct_tcr(WipeTowerWriter2& writer,
                                   bool priming,
                                   size_t old_tool,
								   bool is_finish,
                                   bool is_contact = false) const;

    static bool use_gap_wall(const PrintConfig& config);

    static bool wait_for_temp_enabled(const PrintConfig& config);

    WipeTower2(const PrintConfig& config,
	          const PrintRegionConfig& default_region_config,
			  int plate_idx, Vec3d plate_origin,
			  const std::vector<std::vector<float>>& wiping_matrix,
			  size_t initial_tool);


    void set_extruder(size_t idx, const PrintConfig& config);

    void plan_toolchange(float z_par, float layer_height_par, unsigned int old_tool, unsigned int new_tool, float wipe_volume = 0.f);

	void generate(std::vector<std::vector<WipeTower::ToolChangeResult>> &result);

    float get_depth() const { return m_wipe_tower_depth; }
	std::vector<std::pair<float, float>> get_z_and_depth_pairs() const;
    float get_brim_width() const { return m_wipe_tower_brim_width_real; }
	float get_wipe_tower_height() const { return m_wipe_tower_height; }
    BoundingBoxf get_bbx() const {
        if (m_first_layer_bbx.defined)
            return m_first_layer_bbx;
        const float brim = m_wipe_tower_brim_width_real;
        return BoundingBoxf(Vec2d(-brim, -brim), Vec2d(double(m_wipe_tower_width) + brim, double(m_wipe_tower_depth) + brim));
    }
    Vec2f get_rib_offset() const { return m_rib_offset; }
    float get_rib_width() const { return m_rib_width; }
    float get_rib_length() const { return m_rib_length; }

	void set_layer(
		float print_z,
		float layer_height,
		size_t max_tool_changes,
		bool ,
		bool is_last_layer)
	{
		m_z_pos 				= print_z;
		m_layer_height			= layer_height;
		m_depth_traversed  = 0.f;
        m_current_layer_finished = false;
        m_prev_layer_had_interface = m_current_layer_has_interface;

		
		while (!m_plan.empty() && m_layer_info->z < print_z - WT_EPSILON && m_layer_info+1 != m_plan.end())
			++m_layer_info;
        m_current_layer_has_interface = (m_layer_info != m_plan.end()) && (m_layer_info->toolchanges_depth() > WT_EPSILON);

        m_current_shape = SHAPE_NORMAL;
		if (this->is_first_layer()) {
            m_num_layer_changes = 0;
            m_num_tool_changes 	= 0;
        } else
            ++ m_num_layer_changes;
		
		m_extrusion_flow = extrusion_flow(layer_height);
	}

	const Vec2f& 		 position() const { return m_wipe_tower_pos; }
	float     		 width()    const { return m_wipe_tower_width; }
	bool 	  		 finished() const { return m_max_color_changes == 0; }

	std::vector<WipeTower::ToolChangeResult> prime(
		float 						first_layer_height, 
		const std::vector<unsigned int> &tools,
		bool 						last_wipe_inside_wipe_tower);

    WipeTower::ToolChangeResult tool_change(size_t new_tool);

	WipeTower::ToolChangeResult finish_layer();

	bool 			 layer_finished() const {
        return m_current_layer_finished;
	}

    std::vector<float> get_used_filament() const { return m_used_filament_length; }
    std::vector<std::pair<float, std::vector<float>>> get_used_filament_until_layer() const { return m_used_filament_length_until_layer; }
    int get_number_of_toolchanges() const { return m_num_tool_changes; }

    struct FilamentParameters {
        std::string 	    material = "PLA";
        bool                is_soluble = false;
        bool                is_support = false;
        int  			    temperature = 0;
        int  			    first_layer_temperature = 0;
        int                 interface_print_temperature = 0;
        float               loading_speed = 0.f;
        float               loading_speed_start = 0.f;
        float               unloading_speed = 0.f;
        float               unloading_speed_start = 0.f;
        float               delay = 0.f ;

		float               filament_stamping_loading_speed = 0.f;
		float               filament_stamping_distance = 0.f;

        int                 cooling_moves = 0;
        float               cooling_initial_speed = 0.f;
        float               cooling_final_speed = 0.f;
        float               ramming_line_width_multiplicator = 1.f;
        float               ramming_step_multiplicator = 1.f;
        float               max_e_speed = std::numeric_limits<float>::max();
        std::vector<float>  ramming_speed;
        float               nozzle_diameter;
        float               filament_area;
		bool			    multitool_ramming;
		float               multitool_ramming_time = 0.f;
		float               filament_minimal_purge_on_wipe_tower = 0.f;
        float               retract_length;
        float               retract_speed;
        float               tower_interface_pre_extrusion_dist = 0.f;
        float               tower_interface_pre_extrusion_length = 0.f;
        float               tower_ironing_area = 4.f;
        float               tower_interface_purge_length = 0.f;
        float               max_layer_height = 0.f;
    };

private:
	enum wipe_shape
	{
		SHAPE_NORMAL = 1,
		SHAPE_REVERSED = -1
	};

    const float Width_To_Nozzle_Ratio = 1.25f;
    const float WT_EPSILON            = 1e-3f;
    float filament_area() const {
        return m_filpar[0].filament_area;
    }


	bool   m_semm               = true;
	bool   m_enable_filament_ramming = true;
	bool   m_is_mk4mmu3         = false;
    int    m_wipe_tower_filament = 0;
    Vec2f  m_wipe_tower_pos;
	float  m_wipe_tower_width;
	float  m_wipe_tower_depth 	= 0.f;
	float  m_wipe_tower_height  = 0.f;
	float  m_wipe_tower_cone_angle = 0.f;
    float  m_wipe_tower_brim_width      = 0.f;
    float  m_wipe_tower_brim_width_real = 0.f;
    BoundingBoxf m_first_layer_bbx;
	float  m_wipe_tower_rotation_angle = 0.f;
    float  m_internal_rotation  = 0.f;
	float  m_y_shift			= 0.f;
	float  m_z_pos 				= 0.f;
	float  m_layer_height 		= 0.f;
	size_t m_max_color_changes 	= 0;
    int    m_old_temperature    = -1;
    float  m_travel_speed       = 0.f;
	float  m_infill_speed       = 0.f;
    float  m_wipe_tower_max_purge_speed   = 90.f;
	float  m_perimeter_speed    = 0.f;
    float  m_first_layer_speed  = 0.f;
    size_t m_first_layer_idx    = size_t(-1);
    bool   m_enable_tower_interface_features = false;
    bool   m_enable_tower_interface_cooldown_during_tower = false;
    bool   m_wait_for_temp_on_wipe_tower = false;
    bool   m_prev_layer_had_interface = false;
    bool   m_current_layer_has_interface = false;

	int m_wall_type;
    bool   m_used_fillet                  = true;
    float  m_rib_width                    = 10;
    float  m_extra_rib_length             = 0;
    float  m_rib_length                   = 0;
    Vec2f  m_rib_offset                   = Vec2f::Zero();
    bool   m_use_gap_wall                 = false;
    std::vector<std::vector<Vec2f>> m_wall_skip_points;

    bool   m_enable_arc_fitting           = false;

    float           m_cooling_tube_retraction   = 0.f;
    float           m_cooling_tube_length       = 0.f;
    float           m_parking_pos_retraction    = 0.f;
    float           m_extra_loading_move        = 0.f;
    float           m_bridging                  = 0.f;
    bool            m_sparse_layers_skipped     = false;
    bool            m_sparse_layers_combined    = false;
    bool            m_set_extruder_trimpot      = false;
    bool            m_adhesion                  = true;
    GCodeFlavor     m_gcode_flavor;

    enum {
        RectangularBed,
        CircularBed,
        CustomBed
    } m_bed_shape;
    float m_bed_width;
    Vec2f m_bed_bottom_left;
    Polygon m_bed_polygon;

	float m_perimeter_width = 0.4f * Width_To_Nozzle_Ratio;
	float m_extrusion_flow = 0.038f;
    std::vector<FilamentParameters> m_filpar;

	unsigned int m_num_layer_changes = 0;
	unsigned int m_num_tool_changes  = 0;
	wipe_shape   	m_current_shape = SHAPE_NORMAL;
    size_t 	m_current_tool  = 0;
    const std::vector<std::vector<float>> wipe_volumes;

	float           m_depth_traversed = 0.f;
    bool            m_current_layer_finished = false;
	bool 			m_left_to_right   = true;
	float			m_extra_flow      = 1.f;
	float			m_extra_spacing_wipe    = 1.f;
	float			m_extra_spacing_ramming = 1.f;

    bool is_first_layer() const { return size_t(m_layer_info - m_plan.begin()) == m_first_layer_idx; }

    float wipe_row_spacing(bool first_layer) const { return (first_layer ? m_extra_flow : m_extra_spacing_wipe) * m_perimeter_width; }
    float wipe_line_width() const { return m_perimeter_width * m_extra_flow; }

    bool tool_ramming_enabled(size_t tool) const { return (m_semm && m_enable_filament_ramming) || m_filpar[tool].multitool_ramming; }
    bool boundary_wipe_start_enabled(size_t tool) const { return tool_ramming_enabled(tool) && !m_semm && m_use_gap_wall; }

    float wipe_start_offset_after_ram(float ramming_depth, bool first_layer) const
    {
        return ramming_depth + wipe_row_spacing(first_layer) - (m_perimeter_width + wipe_line_width()) / 2.f;
    }

    Vec2f toolchange_entry_pos(float depth_traversed, float ramming_depth, bool first_layer) const
    {
        Vec2f pos(m_perimeter_width / 2.f, m_perimeter_width / 2.f + depth_traversed);
        if (!m_semm && m_use_gap_wall && ramming_depth > 0.f)
            pos.y() += wipe_start_offset_after_ram(ramming_depth, first_layer);
        return pos;
    }

	float extrusion_flow(float layer_height = -1.f) const
	{
		if ( layer_height < 0 )
			return m_extrusion_flow;
		return layer_height * ( m_perimeter_width - layer_height * (1.f-float(M_PI)/4.f)) / filament_area();
	}


	void plan_tower();

    void save_on_last_wipe();

	struct WipeTowerInfo{
		struct ToolChange {
            size_t old_tool;
            size_t new_tool;
			float required_depth;
            float ramming_depth;
            float first_wipe_line;
            float wipe_volume;
			float wipe_volume_total;
            ToolChange(size_t old, size_t newtool, float depth=0.f, float ramming_depth=0.f, float fwl=0.f, float wv=0.f)
            : old_tool{old}, new_tool{newtool}, required_depth{depth}, ramming_depth{ramming_depth}, first_wipe_line{fwl}, wipe_volume{wv}, wipe_volume_total{wv} {}
		};
		float z;
		float height;
		float depth;
		bool  combined_away{false};
		float toolchanges_depth() const { float sum = 0.f; for (const auto &a : tool_changes) sum += a.required_depth; return sum; }

		std::vector<ToolChange> tool_changes;

		WipeTowerInfo(float z_par, float layer_height_par)
			: z{z_par}, height{layer_height_par}, depth{0} {}
	};

	std::vector<WipeTowerInfo> m_plan;
	std::vector<WipeTowerInfo>::iterator m_layer_info = m_plan.end();

	float m_current_height = 0.f;

    std::vector<float> m_used_filament_length;
	std::vector<std::pair<float, std::vector<float>>> m_used_filament_length_until_layer;

    int first_toolchange_to_nonsoluble_nonsupport(
            const std::vector<WipeTowerInfo::ToolChange>& tool_changes) const;

	void toolchange_Unload(
		WipeTowerWriter2 &writer,
		const WipeTower::box_coordinates  &cleaning_box, 
		const std::string&	 	current_material,
		const int 				old_temperature,
		const int 				new_temperature);

	void toolchange_Change(
		WipeTowerWriter2 &writer,
        const size_t		new_tool,
		const std::string& 		new_material,
		const int 				wait_for_temp,
		const bool 				wait_beside_tower);
	
	void toolchange_Load(
		WipeTowerWriter2 &writer,
		const WipeTower::box_coordinates  &cleaning_box);
	
	void toolchange_Wipe(
		WipeTowerWriter2 &writer,
		const WipeTower::box_coordinates  &cleaning_box,
		float wipe_volume,
        bool interface_layer,
        bool priming = false,
        bool fill_box = false);


    Polygon generate_support_rib_wall(WipeTowerWriter2&                 writer,
                                      const WipeTower::box_coordinates& wt_box,
                                      double                 feedrate,
                                      bool                   first_layer,
                                      bool                   rib_wall,
                                      bool                   extrude_perimeter);

    Polygon generate_support_cone_wall(
        WipeTowerWriter2& writer, 
		const WipeTower::box_coordinates& wt_box, 
		double feedrate, 
		bool infill_cone, 
		float spacing);

    Polygon generate_rib_polygon(const WipeTower::box_coordinates& wt_box);

    void compute_wall_skip_points();

    WipeTowerInfo::ToolChange set_toolchange(size_t old_tool, size_t new_tool, float layer_height, float wipe_volume, bool first_layer_plan);
};




}

#endif
