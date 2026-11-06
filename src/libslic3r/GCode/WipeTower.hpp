#ifndef WipeTower_
#define WipeTower_

#include <cmath>
#include <map>
#include <cstddef>
#include <math.h>
#include <limits>
#include <string>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <algorithm>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include <unordered_set>
#include <vector>
#include "libslic3r/MultiNozzleUtils.hpp"
namespace Slic3r
{

class WipeTowerWriter;
class PrintConfig;
enum GCodeFlavor : unsigned char;




Polylines construct_gap_for_skip_points(
    const Polygon& polygon, const std::vector<Vec2f>& skip_points, float wt_width, float gap_length, Polygon& insert_skip_polygon);




const char* flush_planner_queue_command(GCodeFlavor flavor);
std::string wait_command(GCodeFlavor flavor, float seconds);

class WipeTower
{
public:
    friend class WipeTowerWriter;
    static const std::string never_skip_tag() { return "_GCODE_WIPE_TOWER_NEVER_SKIP_TAG"; }


	static const std::map<float, float> min_depth_per_height;
    static float get_limit_depth_by_height(float max_height);
    static float get_auto_brim_by_height(float max_height);




    static float estimate_brim_real_width(float brim_width, float nozzle_diameter, float first_layer_height, bool type2);

    static float get_wrapping_detection_depth();

    static float nozzle_change_perimeter_width(float nozzle_diameter);
    static TriangleMesh                 its_make_rib_tower(float width, float depth, float height, float rib_length, float rib_width, bool fillet_wall);
    static TriangleMesh                 its_make_rib_brim(const Polygon& brim, float layer_height);
    static Polygon                      rib_section(float width, float depth, float rib_length, float rib_width, bool fillet_wall);

    struct PurgeEstimate
    {
        float prime_volume           = 0.f;
        int   category               = 0;
        float filament_change_length = 0.f;
        float filament_diameter      = 1.75f;
    };



    static float estimate_tower_blocks_depth(const std::vector<PurgeEstimate> &purges, float width, float layer_height, float nozzle_diameter, float extra_spacing);


    static float rib_footprint_side(float width, float depth, float rib_width, float extra_rib_length, float max_height);


    static float estimate_rib_tower_bbox_side(const std::vector<PurgeEstimate> &purges, float width, float layer_height, float nozzle_diameter, float extra_spacing, float rib_width, float extra_rib_length, float max_height);




    static Vec2f                        move_box_inside_polygon(const BoundingBox &box, const Polygons &polygons, coord_t offset = 0);
    static Polygon                      rounding_polygon(Polygon &polygon, double rounding = 2., double angle_tol = 30. / 180. * PI);
    struct Extrusion
    {
		Extrusion(const Vec2f &pos, float width, unsigned int tool) : pos(pos), width(width), tool(tool) {}

		Vec2f				pos;


		float 			width;

		unsigned int    tool;
	};

	struct NozzleChangeResult
    {
        std::string gcode;

        Vec2f start_pos;
        Vec2f end_pos;

		Vec2f origin_start_pos;

        std::vector<Vec2f> wipe_path;
        bool is_extruder_change{true};
    };

	struct ToolChangeResult
	{

		float					print_z;
		float 					layer_height;

		std::string				gcode;

		std::vector<Extrusion> 	extrusions;


		Vec2f						start_pos;


		Vec2f						end_pos;


		float  				    elapsed_time;


        bool                    priming;

		bool                    is_tool_change{false};
		Vec2f                   tool_change_start_pos;






        std::vector<Vec2f> wipe_path;


        float purge_volume = 0.f;


        int initial_tool;


        int new_tool;




        bool is_finish_first = false;

        bool               is_contact = false;
        NozzleChangeResult nozzle_change_result;



        bool               combined_away = false;


		float total_extrusion_length_in_plane() {
			float e_length = 0.f;
			for (size_t i = 1; i < this->extrusions.size(); ++ i) {
				const Extrusion &e = this->extrusions[i];
				if (e.width > 0) {
					Vec2f v = e.pos - (&e - 1)->pos;
					e_length += v.norm();
				}
			}
			return e_length;
		}


		bool force_travel = false;
	};

    struct box_coordinates
    {
        box_coordinates(float left, float bottom, float width, float height) :
            ld(left        , bottom         ),
            lu(left        , bottom + height),
            rd(left + width, bottom         ),
            ru(left + width, bottom + height) {}
        box_coordinates(const Vec2f &pos, float width, float height) : box_coordinates(pos(0), pos(1), width, height) {}
        void translate(const Vec2f &shift) {
            ld += shift; lu += shift;
            rd += shift; ru += shift;
        }
        void translate(const float dx, const float dy) { translate(Vec2f(dx, dy)); }
        void expand(const float offset) {
            ld += Vec2f(- offset, - offset);
            lu += Vec2f(- offset,   offset);
            rd += Vec2f(  offset, - offset);
            ru += Vec2f(  offset,   offset);
        }
        void expand(const float offset_x, const float offset_y) {
            ld += Vec2f(- offset_x, - offset_y);
            lu += Vec2f(- offset_x,   offset_y);
            rd += Vec2f(  offset_x, - offset_y);
            ru += Vec2f(  offset_x,   offset_y);
        }
        Vec2f ld;
        Vec2f lu;
        Vec2f rd;
        Vec2f ru;
    };



    ToolChangeResult construct_tcr(WipeTowerWriter& writer,
                                   bool priming,
                                   size_t old_tool,
                                   bool is_finish,
		                           bool is_tool_change, float purge_volume, bool is_contact) const;

    ToolChangeResult construct_block_tcr(WipeTowerWriter& writer,
                                   bool priming,
                                   size_t filament_id,
                                   bool is_finish, float purge_volume) const;







	WipeTower(const PrintConfig& config, int plate_idx, Vec3d plate_origin, size_t initial_tool, const float wipe_tower_height, const std::vector<unsigned int>& slice_used_filaments);



    void set_extruder(size_t idx, const PrintConfig& config);

    void set_shared_print_bed(const Polygons &bed) { m_shared_print_bed = bed; }


    void set_has_filament_switcher(bool v) { m_has_filament_switcher = v; }


    void plan_toolchange(float z_par, float layer_height_par, unsigned int old_tool, unsigned int new_tool, float wipe_volume_ec = 0.f, float wipe_volume_nc = 0.f, float prime_volume = 0.f);



	void generate(std::vector<std::vector<ToolChangeResult>> &result);

	WipeTower::ToolChangeResult only_generate_out_wall(bool is_new_mode = false);
    Polygon generate_support_wall(WipeTowerWriter &writer, const box_coordinates &wt_box, double feedrate, bool first_layer);
    Polygon generate_support_wall_new(WipeTowerWriter &writer, const box_coordinates &wt_box, double feedrate, bool first_layer,bool rib_wall, bool extrude_perimeter, bool skip_points);

    Polygon generate_rib_polygon(const box_coordinates &wt_box);
    float get_depth() const { return m_wipe_tower_depth; }
    float get_brim_width() const { return m_wipe_tower_brim_width_real; }
    BoundingBoxf get_bbx() const {
        if (m_outer_wall.empty()) return BoundingBoxf({Vec2d(0,0)});
        BoundingBox  box = get_extents(m_outer_wall.begin()->second);
        BoundingBoxf res = BoundingBoxf(unscale(box.min), unscale(box.max));
        return res;
    }
    std::map<float, Polylines> get_outer_wall() const
    {
        return m_outer_wall;
    }
    float get_height() const { return m_wipe_tower_height; }
    float get_layer_height() const { return m_layer_height; }
    float get_rib_length() const { return m_rib_length; }
    float get_rib_width() const { return m_rib_width; }

	void set_last_layer_extruder_fill(bool extruder_fill) {
        if (!m_plan.empty()) {
			m_plan.back().extruder_fill = extruder_fill;
		}
	}



	void set_layer(

		float print_z,

		float layer_height,

		size_t max_tool_changes,

		bool is_first_layer,

		bool is_last_layer)
	{
		m_z_pos 				= print_z;
		m_layer_height			= layer_height;
		m_depth_traversed  = 0.f;
        m_current_layer_finished = false;

		m_current_shape = SHAPE_NORMAL;
		if (is_first_layer) {
            m_num_layer_changes = 0;
            m_num_tool_changes 	= 0;
        } else
            ++ m_num_layer_changes;


		m_extrusion_flow = extrusion_flow(layer_height);

		while (!m_plan.empty() && m_layer_info->z < print_z - WT_EPSILON && m_layer_info+1 != m_plan.end())
			++m_layer_info;
	}


	const Vec2f& 		 position() const { return m_wipe_tower_pos; }

	float     		 width()    const { return m_wipe_tower_width; }

	bool 	  		 finished() const { return m_max_color_changes == 0; }


	std::vector<ToolChangeResult> prime(

		float 						initial_layer_print_height,

		const std::vector<unsigned int> &tools,


		bool 						last_wipe_inside_wipe_tower);




    ToolChangeResult tool_change(size_t new_tool, bool extrude_perimeter = false, bool first_toolchange_to_nonsoluble = false);

	NozzleChangeResult nozzle_change(int old_filament_id, int new_filament_id);



    ToolChangeResult finish_layer(bool extruder_perimeter = true, bool extruder_fill = true);


    float extrusion_flow(float layer_height = -1.f) const
    {
        if (layer_height < 0) return m_extrusion_flow;
        return layer_height * (m_perimeter_width - layer_height * (1.f - float(M_PI) / 4.f)) / filament_area();
    }
    float nozzle_change_extrusion_flow(float layer_height = -1.f) const
    {
        if (layer_height < 0)
            return m_extrusion_flow;
        return layer_height * (m_nozzle_change_perimeter_width - layer_height * (1.f - float(M_PI) / 4.f)) / filament_area();
    }

	bool get_floating_area(float& start_pos_y, float& end_pos_y) const;
	bool need_thick_bridge_flow(float pos_y) const;
    float get_extrusion_flow() const { return m_extrusion_flow; }


	bool 			 layer_finished() const {
        return m_current_layer_finished;
	}

    std::vector<float> get_used_filament() const { return m_used_filament_length; }
    int get_number_of_toolchanges() const { return m_num_tool_changes; }

	void set_has_tpu_filament(bool has_tpu) { m_has_tpu_filament = has_tpu; }

    bool has_tpu_filament() const { return m_has_tpu_filament; }
    struct FilamentParameters {
        std::string 	    material = "PLA";
        int                 category;
        bool                is_soluble = false;

        bool                is_support = false;
        int  			    nozzle_temperature = 0;
        int  			    nozzle_temperature_initial_layer = 0;









        float               ramming_line_width_multiplicator = 1.f;
        float               ramming_step_multiplicator = 1.f;
        float               max_e_speed = std::numeric_limits<float>::max();
        std::vector<float>  ramming_speed;
        float               nozzle_diameter;
        float               filament_area;
        float               retract_length;
        float               retract_speed;
        float               wipe_dist;
        std::pair<float,float>  max_e_ramming_speed;
        std::pair<float, float> ramming_travel_time;
        std::pair<std::vector<float>,std::vector<float>>  precool_t;
        std::pair<std::vector<float>, std::vector<float>> precool_t_first_layer;
        std::pair<int,int>    precool_target_temp;
        float filament_cooling_before_tower = 0.f;
        float flat_iron_area;
        float filament_tower_interface_print_temp;
        float filament_tower_interface_pre_extrusion_dist = 0;
        float filament_tower_interface_pre_extrusion_length = 0;
        float filament_petg_pre_extrusion_offset_dist = 0;

        float max_layer_height = 0.f;
    };


    void set_used_filament_ids(const std::vector<int> &used_filament_ids) { m_used_filament_ids = used_filament_ids; };
    void set_filament_categories(const std::vector<int> & filament_categories) { m_filament_categories = filament_categories;};
    void set_nozzle_group_result(const MultiNozzleUtils::LayeredNozzleGroupResult &multi_nozzle_group_result) { m_multi_nozzle_group_result = &multi_nozzle_group_result; };
    std::vector<int> m_used_filament_ids;
    std::vector<int> m_filament_categories;
    const MultiNozzleUtils::LayeredNozzleGroupResult *m_multi_nozzle_group_result{nullptr};

    enum class WipeTowerLayerType : unsigned char { Normal, Contact, Solid, Contact_UP};

	struct WipeTowerBlock
    {
        int              block_id{0};
        int              filament_adhesiveness_category{0};
        std::vector<float>      layer_depths;

        std::vector<float>      finish_depth{0};
        std::vector<WipeTowerLayerType> layers_type;
        float            depth{0};
        float            start_depth{0};
        float            cur_depth{0};
        int              last_filament_change_id{-1};
        int              last_nozzle_change_id{-1};
	};

	struct BlockDepthInfo
    {
        int category{-1};
        float depth{0};
        float nozzle_change_depth{0};
	};

	std::vector<std::vector<BlockDepthInfo>> m_all_layers_depth;
	std::vector<WipeTowerBlock> m_wipe_tower_blocks;
    int                  m_last_block_id;
    WipeTowerBlock*      m_cur_block{nullptr};


    WipeTowerBlock* get_block_by_category(int filament_adhesiveness_category, bool create);
    void add_depth_to_block(int filament_id, int filament_adhesiveness_category, float depth, bool is_nozzle_change = false);
	int get_filament_category(int filament_id);
	void reset_block_status();
    int get_wall_filament_for_all_layer();

    void generate_new(std::vector<std::vector<WipeTower::ToolChangeResult>> &result);

	void plan_tower_new();
	void generate_wipe_tower_blocks(bool add_solid_flag);
    void update_all_layer_depth(float wipe_tower_depth);
    void set_nozzle_last_layer_id();
    void set_first_layer_flow_ratio(const float flow_ratio);


    void set_accelerations(const std::vector<double> &normal, const std::vector<double> &first_layer_normal,
                           const std::vector<double> &travel, const std::vector<double> &first_layer_travel);
    void calc_block_infill_gap();
    ToolChangeResult   tool_change_new(size_t new_tool, bool solid_change = false, bool solid_nozzlechange=false);
    NozzleChangeResult ramming(int old_filament_id, int new_filament_id, bool solid_change = false, bool extruder_change = true);
    ToolChangeResult   finish_layer_new(bool extrude_perimeter = true, bool extrude_fill = true, bool extrude_fill_wall = true);
    ToolChangeResult   finish_block(const WipeTowerBlock &block, int filament_id, bool extrude_fill = true);
    ToolChangeResult   finish_block_solid(const WipeTowerBlock &block, int filament_id, bool extrude_fill = true, WipeTowerLayerType layer_type = WipeTowerLayerType::Normal);
    void toolchange_wipe_new(WipeTowerWriter &writer, const box_coordinates &cleaning_box, float wipe_length,bool solid_toolchange=false);
    Vec2f              get_rib_offset() const { return m_rib_offset; }
    bool               is_need_ramming(int filament_id_1, int filament_id_2, int layer_id) const;
    bool               is_same_extruder(int filament_id_1, int filament_id_2, int layer_id) const;
    bool               is_same_nozzle(int filament_id_1, int filament_id_2, int layer_id) const;
    int                get_nozzle_id(int filament_id, int layer_id) const;
    int                get_extruder_id(int filament_id, int layer_id) const;

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

    int    m_slice_used_filaments      = 0;
    int    m_wrapping_detection_layers = 0;
    bool   m_enable_wrapping_detection = false;
	bool   m_enable_timelapse_print = false;
	bool   m_semm               = true;
    Vec2f  m_wipe_tower_pos;
	float  m_wipe_tower_width;
	float  m_wipe_tower_depth 	= 0.f;

	float  m_wipe_tower_height = 0.f;
    float  m_wipe_tower_brim_width      = 0.f;
    float  m_wipe_tower_brim_width_real = 0.f;
	float  m_wipe_tower_rotation_angle = 0.f;
    float  m_internal_rotation  = 0.f;
	float  m_y_shift			= 0.f;
	float  m_z_pos 				= 0.f;
	float  m_layer_height 		= 0.f;
	size_t m_max_color_changes 	= 0;
    int    m_old_temperature    = -1;
    float  m_travel_speed       = 0.f;
    float  m_first_layer_speed  = 0.f;
    size_t m_first_layer_idx    = size_t(-1);
    Vec2f            m_origin;
    std::vector<int>    m_last_layer_id;
    std::pair<std::vector<double>,std::vector<double>> m_filaments_change_length;
    size_t       m_cur_layer_id;
    NozzleChangeResult m_nozzle_change_result;
    bool               m_has_tpu_filament{false};
    bool               m_is_multi_extruder{false};
    bool               m_use_gap_wall{false};
    bool               m_use_rib_wall{false};
    float              m_rib_length=0.f;
    float              m_rib_width=0.f;
    float              m_extra_rib_length=0.f;
    bool               m_used_fillet{false};
    Vec2f              m_rib_offset{Vec2f(0.f, 0.f)};
    bool               m_tower_framework{false};
    bool               m_need_reverse_travel{false};
    bool               m_enable_tower_interface_features{false};






    float           m_bridging                  = 0.f;
    bool            m_sparse_layers_skipped     = false;
    bool            m_sparse_layers_combined    = false;


    bool            m_adhesion                  = true;
    GCodeFlavor     m_gcode_flavor;
    bool                      m_is_multiple_nozzle = false;
    std::vector<unsigned int> m_normal_accels;
    std::vector<unsigned int> m_first_layer_normal_accels;
    std::vector<unsigned int> m_travel_accels;
    std::vector<unsigned int> m_first_layer_travel_accels;
    unsigned int              m_max_accels;
    bool                      m_accel_to_decel_enable;
    float                     m_accel_to_decel_factor;
    bool                      m_enable_arc_fitting = true;
    std::vector<double>       m_hotend_heating_rate;
    std::vector<double>       m_hotend_cooling_rate;
    Polygons                  m_shared_print_bed;


    enum {
        RectangularBed,
        CircularBed,
        CustomBed
    } m_bed_shape;
    float m_bed_width;
    Vec2f m_bed_bottom_left;

    float m_first_layer_flow_ratio;
	float m_perimeter_width = 0.4f * Width_To_Nozzle_Ratio;
    float m_nozzle_change_perimeter_width = 0.4f * Width_To_Nozzle_Ratio;
	float m_extrusion_flow = 0.038f;
    std::unordered_map<int, std::pair<float,float>> m_block_infill_gap_width;

    std::vector<FilamentParameters> m_filpar;



	unsigned int m_num_layer_changes = 0;
	unsigned int m_num_tool_changes  = 0;

	bool m_print_brim = true;

	wipe_shape   	m_current_shape = SHAPE_NORMAL;
    size_t 	m_current_tool  = 0;



	float           m_depth_traversed = 0.f;
    bool            m_current_layer_finished = false;
	bool 			m_left_to_right   = true;
	float			m_extra_spacing   = 1.f;
	float           m_tpu_fixed_spacing = 2;
    float           m_max_speed = 5400.f;
    std::vector<std::vector<Vec2f>> m_wall_skip_points;
    std::map<float,Polylines> m_outer_wall;
    std::vector<double>        m_printable_height;
    bool is_first_layer() const { return size_t(m_layer_info - m_plan.begin()) == m_first_layer_idx; }
    bool                       is_valid_last_layer(int tool, int layer_id, double layer_z) const;
    bool                       m_flat_ironing=false;
    bool                       m_contact_ironing = false;
    bool                       m_has_filament_switcher = false;
    float                      m_contact_speed   = 20 * 60.f;
    std::vector<int>           m_physical_extruder_map;

	float volume_to_length(float volume, float line_width, float layer_height) const {
		return std::max(0.f, volume / (layer_height * (line_width - layer_height * (1.f - float(M_PI) / 4.f))));
	}

    float length_to_volume(float length,float line_width, float layer_height) const
    {
        return std::max(0.f, length * (layer_height * (line_width - layer_height * (1.f - float(M_PI) / 4.f))));
    }

	void plan_tower();

	bool layer_is_printed(bool toolchanges_on_layer) const;


	void make_wipe_tower_square();

	Vec2f get_next_pos(const WipeTower::box_coordinates &cleaning_box, float wipe_length, bool solid_toolchange);


    void save_on_last_wipe();

	bool is_tpu_filament(int filament_id) const;
	bool is_petg_filament(int filament_id) const;
    bool is_need_reverse_travel(int filament, bool extruder_change) const;

	box_coordinates align_perimeter(const box_coordinates& perimeter_box);

    void set_for_wipe_tower_writer(WipeTowerWriter &writer);


	struct WipeTowerInfo{
		struct ToolChange {
            size_t old_tool;
            size_t new_tool;
			float required_depth;
            float ramming_depth;
            float first_wipe_line;
            float wipe_volume;
			float wipe_length;
            float nozzle_change_depth{0};
            float nozzle_change_length{0};

			float purge_volume;
            ToolChange(size_t old, size_t newtool, float depth=0.f, float ramming_depth=0.f, float fwl=0.f, float wv=0.f, float wl = 0, float pv = 0)
				: old_tool{ old }, new_tool{ newtool }, required_depth{ depth }, ramming_depth{ ramming_depth }, first_wipe_line{ fwl }, wipe_volume{ wv }, wipe_length{ wl }, purge_volume{ pv } {}
		};
		float z;
		float height;
		float depth;
		float extra_spacing;
        bool  extruder_fill{true};

		bool  combined_away{false};
		float toolchanges_depth() const { float sum = 0.f; for (const auto &a : tool_changes) sum += a.required_depth; return sum; }

		std::vector<ToolChange> tool_changes;

		WipeTowerInfo(float z_par, float layer_height_par)
			: z{z_par}, height{layer_height_par}, depth{0}, extra_spacing{1.f} {}
	};

	std::vector<WipeTowerInfo> m_plan;
	std::vector<WipeTowerInfo>::iterator m_layer_info = m_plan.end();


    std::vector<float> m_used_filament_length;




    int first_toolchange_to_nonsoluble_nonsupport(
            const std::vector<WipeTowerInfo::ToolChange>& tool_changes) const;
    WipeTowerInfo::ToolChange set_toolchange(int old_tool, int new_tool, float layer_height, float wipe_volume, float purge_volume,int layer_id);
	void toolchange_Unload(
		WipeTowerWriter &writer,
		const box_coordinates  &cleaning_box,
		const std::string&	 	current_material,
		const int 				new_temperature);

	void toolchange_Change(
		WipeTowerWriter &writer,
        const size_t		new_tool,
		const std::string& 		new_material);

	void toolchange_Load(
		WipeTowerWriter &writer,
		const box_coordinates  &cleaning_box);

	void toolchange_Wipe(
		WipeTowerWriter &writer,
		const box_coordinates  &cleaning_box,
		float wipe_volume);
    void get_wall_skip_points(const WipeTowerInfo &layer,int layer_id);
    void get_all_wall_skip_points();
    ToolChangeResult merge_tcr(ToolChangeResult &first, ToolChangeResult &second);
    float            get_block_gap_width(int tool, bool is_nozzlechangle = false);
};










bool wipe_tower_sparse_layers_skipped(const PrintConfig &config);


bool wipe_tower_layer_is_sparse(const std::vector<WipeTower::ToolChangeResult> &layer_tool_changes);




std::vector<float> compute_compacted_wipe_tower_z(const std::vector<std::vector<WipeTower::ToolChangeResult>> &tool_changes,
                                                  float base_z = 0.f);










bool wipe_tower_sparse_layers_combined(const PrintConfig &config);


bool wipe_tower_layer_is_combined_away(const std::vector<WipeTower::ToolChangeResult> &layer_tool_changes);






std::vector<char> combine_sparse_wipe_tower_layers(std::vector<float>       &layer_height,
                                                   const std::vector<char>  &layer_is_sparse,
                                                   const std::vector<float> &max_layer_height,
                                                   size_t                    first_layer_idx);



template<class PlanLayers, class FilamentParams>
void combine_sparse_wipe_tower_plan(PlanLayers &plan, const FilamentParams &filpar, size_t first_layer_idx, size_t initial_tool)
{
    const size_t       n = plan.size();
    std::vector<float> heights(n);
    std::vector<char>  sparse(n);
    std::vector<float> caps(n);


    size_t tool = initial_tool;
    for (const auto &layer : plan)
        if (! layer.tool_changes.empty()) {
            tool = layer.tool_changes.front().old_tool;
            break;
        }
    for (size_t i = 0; i < n; ++i) {
        heights[i] = plan[i].height;
        sparse[i]  = plan[i].tool_changes.empty() ? 1 : 0;
        caps[i]    = tool < filpar.size() ? filpar[tool].max_layer_height : 0.f;
        if (! plan[i].tool_changes.empty())
            tool = plan[i].tool_changes.back().new_tool;
    }

    const std::vector<char> combined_away = combine_sparse_wipe_tower_layers(heights, sparse, caps, first_layer_idx);
    for (size_t i = 0; i < n; ++i) {
        plan[i].height        = heights[i];
        plan[i].combined_away = combined_away[i] != 0;
    }
}


}

#endif
