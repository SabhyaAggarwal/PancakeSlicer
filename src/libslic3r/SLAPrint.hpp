#ifndef slic3r_SLAPrint_hpp_
#define slic3r_SLAPrint_hpp_

#include <cstddef>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <functional>
#include <mutex>
#include "ObjectID.hpp"
#include <vector>
#include "ExPolygon.hpp"
#include <type_traits>
#include "Config.hpp"
#include <string>
#include "Polygon.hpp"
#include <utility>
#include "PrintBase.hpp"
#include "PrintConfig.hpp"
#include "SLA/Hollowing.hpp"
#include "SLA/JobController.hpp"
#include "SLA/Pad.hpp"
#include "SLA/RasterBase.hpp"
#include "SLA/SupportPoint.hpp"
#include "SLA/SupportTree.hpp"
#include "Execution/ExecutionTBB.hpp"
#include "Point.hpp"
#include "MTUtils.hpp"
#include "Zipper.hpp"
#include "libslic3r.h"

namespace Slic3r {

enum SLAPrintStep : unsigned int {
    slapsMergeSlicesAndEval,
    slapsRasterize,
	slapsCount
};

enum SLAPrintObjectStep : unsigned int {
    slaposHollowing,
    slaposDrillHoles,
	slaposObjectSlice,
	slaposSupportPoints,
	slaposSupportTree,
	slaposPad,
    slaposSliceSupports,
	slaposCount
};

class SLAPrint;
class GLCanvas;

using _SLAPrintObjectBase =
    PrintObjectBaseWithState<SLAPrint, SLAPrintObjectStep, slaposCount>;

enum SliceOrigin { soSupport, soModel };

class SLAPrintObject : public _SLAPrintObjectBase
{
private:
    using Inherited = _SLAPrintObjectBase;

public:

    SLAPrintObject(const SLAPrintObject&) = delete;
    SLAPrintObject& operator=(const SLAPrintObject&) = delete;

    const SLAPrintObjectConfig& config() const { return m_config; }
    const Transform3d&          trafo()  const { return m_trafo; }
    bool                        is_left_handed() const { return m_left_handed; }

    struct Instance {
        Instance(ObjectID inst_id, const Point &shft, float rot) : instance_id(inst_id), shift(shft), rotation(rot) {}
        bool operator==(const Instance &rhs) const { return this->instance_id == rhs.instance_id && this->shift == rhs.shift && this->rotation == rhs.rotation; }
        ObjectID instance_id;
        Point 	shift;
        float 	rotation;
    };
    const std::vector<Instance>& instances() const { return m_instances; }

    bool                    has_mesh(SLAPrintObjectStep step) const;
    TriangleMesh            get_mesh(SLAPrintObjectStep step) const;

    const TriangleMesh&     support_mesh() const;
    const TriangleMesh&     pad_mesh() const;

    const indexed_triangle_set &hollowed_interior_mesh() const;

    const TriangleMesh & get_mesh_to_print() const {
        return (m_hollowing_data && is_step_done(slaposDrillHoles)) ? m_hollowing_data->hollow_mesh_with_holes_trimmed : transformed_mesh();
    }

    const TriangleMesh & get_mesh_to_slice() const {
        return (m_hollowing_data && is_step_done(slaposDrillHoles)) ? m_hollowing_data->hollow_mesh_with_holes : transformed_mesh();
    }

    const TriangleMesh&     transformed_mesh() const;

    sla::SupportPoints      transformed_support_points() const;
    sla::DrainHoles         transformed_drainhole_points() const;

    double get_elevation() const;

    double get_current_elevation() const;

    const std::vector<sla::SupportPoint>& get_support_points() const;

    class SliceRecord {
    public:
        static const size_t NONE = size_t(-1);

        static const SliceRecord EMPTY;

    private:
        coord_t   m_print_z = 0;
        float     m_slice_z = 0.f;
        float     m_height  = 0.f;

        size_t m_model_slices_idx = NONE;
        size_t m_support_slices_idx = NONE;
        const SLAPrintObject *m_po = nullptr;

    public:

        SliceRecord(coord_t key, float slicez, float height):
            m_print_z(key), m_slice_z(slicez), m_height(height) {}

        coord_t print_level() const { return m_print_z; }

        float slice_level() const { return m_slice_z; }

        float layer_height() const { return m_height; }

        bool is_valid() const { return m_po && ! std::isnan(m_slice_z); }

        const SLAPrintObject* print_obj() const { return m_po; }

        void set_model_slice_idx(const SLAPrintObject &po, size_t id) {
            m_po = &po; m_model_slices_idx = id;
        }

        void set_support_slice_idx(const SLAPrintObject& po, size_t id) {
            m_po = &po; m_support_slices_idx = id;
        }

        const ExPolygons& get_slice(SliceOrigin o) const;
        size_t            get_slice_idx(SliceOrigin o) const
        {
            return o == soModel ? m_model_slices_idx : m_support_slices_idx;
        }
    };

private:
    template<class T> inline static T level(const SliceRecord &sr)
    {
        static_assert(std::is_arithmetic<T>::value, "Arithmetic only!");
        return std::is_integral<T>::value ? T(sr.print_level())
                                          : T(sr.slice_level());
    }

    template<class T> inline static SliceRecord create_slice_record(T val)
    {
        static_assert(std::is_arithmetic<T>::value, "Arithmetic only!");
        return std::is_integral<T>::value
                   ? SliceRecord{coord_t(val), 0.f, 0.f}
                   : SliceRecord{0, float(val), 0.f};
    }

    template<class Container, class T>
    static auto closest_slice_record(
            Container& cont,
            T lvl,
            T eps = std::numeric_limits<T>::max()) -> decltype (cont.begin())
    {
        if(cont.empty()) return cont.end();
        if(cont.size() == 1 && std::abs(level<T>(cont.front()) - lvl) > eps)
            return cont.end();

        SliceRecord query = create_slice_record(lvl);

        auto it = std::lower_bound(cont.begin(), cont.end(), query,
                                   [](const SliceRecord& r1,
                                      const SliceRecord& r2)
        {
            return level<T>(r1) < level<T>(r2);
        });

        if(it == cont.end()) return it;

        T diff = std::abs(level<T>(*it) - lvl);

        if(it != cont.begin()) {
            auto it_prev = std::prev(it);
            T diff_prev = std::abs(level<T>(*it_prev) - lvl);
            if(diff_prev < diff) { diff = diff_prev; it = it_prev; }
        }

        if(diff > eps) it = cont.end();

        return it;
    }

    const std::vector<ExPolygons>& get_model_slices() const { return m_model_slices; }
    const std::vector<ExPolygons>& get_support_slices() const;

public:

    const std::vector<SliceRecord>& get_slice_index() const {
        return m_slice_index;
    }

    const SliceRecord& closest_slice_to_print_level(
            coord_t print_level,
            coord_t max_epsilon = std::numeric_limits<coord_t>::max()) const
    {
        auto it = closest_slice_record(m_slice_index, print_level, max_epsilon);
        return it == m_slice_index.end() ? SliceRecord::EMPTY : *it;
    }

    const SliceRecord& closest_slice_to_slice_level(
            float slice_level,
            float max_epsilon = std::numeric_limits<float>::max()) const
    {
        auto it = closest_slice_record(m_slice_index, slice_level, max_epsilon);
        return it == m_slice_index.end() ? SliceRecord::EMPTY : *it;
    }

protected:
    friend class SLAPrint;

	SLAPrintObject(SLAPrint* print, ModelObject* model_object);
    ~SLAPrintObject();

    void                    config_apply(const ConfigBase &other, bool ignore_nonexistent = false) { m_config.apply(other, ignore_nonexistent); }
    void                    config_apply_only(const ConfigBase &other, const t_config_option_keys &keys, bool ignore_nonexistent = false)
        { m_config.apply_only(other, keys, ignore_nonexistent); }

    void                    set_trafo(const Transform3d& trafo, bool left_handed) {
        m_transformed_rmesh.invalidate([this, &trafo, left_handed](){ m_trafo = trafo; m_left_handed = left_handed; });
    }

    template<class InstVec> inline void set_instances(InstVec&& instances) { m_instances = std::forward<InstVec>(instances); }

    bool                    invalidate_step(SLAPrintObjectStep step);
    bool                    invalidate_all_steps();
    bool                    invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys);

    std::vector<bool>                       m_stepmask;

private:
    SLAPrintObjectConfig                    m_config;

    Transform3d                             m_trafo = Transform3d::Identity();
    bool                                    m_left_handed = false;

    std::vector<Instance> 					m_instances;

    std::vector<ExPolygons>                 m_model_slices;

    std::vector<SliceRecord>                m_slice_index;

    std::vector<float>                      m_model_height_levels;

    mutable CachedObject<TriangleMesh>      m_transformed_rmesh;

    class SupportData : public sla::SupportableMesh
    {
    public:
        sla::SupportTree::UPtr  support_tree_ptr;
        std::vector<ExPolygons> support_slices;
        TriangleMesh tree_mesh, pad_mesh, full_mesh;

        inline SupportData(const TriangleMesh &t)
            : sla::SupportableMesh{t.its, {}, {}}
        {}

        sla::SupportTree::UPtr &create_support_tree(const sla::JobController &ctl)
        {
            support_tree_ptr = sla::SupportTree::create(*this, ctl);
            tree_mesh = TriangleMesh{support_tree_ptr->retrieve_mesh(sla::MeshType::Support)};
            return support_tree_ptr;
        }

        void create_pad(const ExPolygons &blueprint, const sla::PadConfig &pcfg)
        {
            if (!support_tree_ptr)
                return;

            support_tree_ptr->add_pad(blueprint, pcfg);
            pad_mesh = TriangleMesh{support_tree_ptr->retrieve_mesh(sla::MeshType::Pad)};
        }
    };

    std::unique_ptr<SupportData> m_supportdata;

    class HollowingData
    {
    public:

        sla::InteriorPtr interior;
        mutable TriangleMesh hollow_mesh_with_holes;
        mutable TriangleMesh hollow_mesh_with_holes_trimmed;
    };

    std::unique_ptr<HollowingData> m_hollowing_data;
};

using PrintObjects = std::vector<SLAPrintObject*>;

using SliceRecord  = SLAPrintObject::SliceRecord;

class TriangleMesh;

struct SLAPrintStatistics
{
    SLAPrintStatistics() { clear(); }
    double                          estimated_print_time;
    double                          objects_used_material;
    double                          support_used_material;
    size_t                          slow_layers_count;
    size_t                          fast_layers_count;
    double                          total_cost;
    double                          total_weight;
    std::vector<double>             layers_times;

    DynamicConfig           config() const;
    static DynamicConfig    placeholders();
    std::string             finalize_output_path(const std::string &path_in) const;

    void clear() {
        estimated_print_time = 0.;
        objects_used_material = 0.;
        support_used_material = 0.;
        slow_layers_count = 0;
        fast_layers_count = 0;
        total_cost = 0.;
        total_weight = 0.;
        layers_times.clear();
    }
};

class SLAArchive {
protected:
    std::vector<sla::EncodedRaster> m_layers;

    virtual std::unique_ptr<sla::RasterBase> create_raster() const = 0;
    virtual sla::RasterEncoder get_encoder() const = 0;

public:
    virtual ~SLAArchive() = default;

    virtual void apply(const SLAPrinterConfig &cfg) = 0;

    template<class Fn, class CancelFn, class EP = ExecutionTBB>
    void draw_layers(
        size_t     layer_num,
        Fn &&      drawfn,
        CancelFn cancelfn = []() { return false; },
        const EP & ep       = {})
    {
        m_layers.resize(layer_num);
        execution::for_each(
            ep, size_t(0), m_layers.size(),
            [this, &drawfn, &cancelfn](size_t idx) {
                if (cancelfn()) return;

                sla::EncodedRaster &enc = m_layers[idx];
                auto                rst = create_raster();
                drawfn(*rst, idx);
                enc = rst->encode(get_encoder());
            },
            execution::max_concurrency(ep));
    }
};

class SLAPrint : public PrintBaseWithState<SLAPrintStep, slapsCount>
{
private:
    typedef PrintBaseWithState<SLAPrintStep, slapsCount> Inherited;

    class Steps;

public:

    SLAPrint(): m_stepmask(slapsCount, true) {}

    virtual ~SLAPrint() override { this->clear(); }

    PrinterTechnology	technology() const noexcept override { return ptSLA; }

    void                clear() override;
    bool                empty() const override { return m_objects.empty(); }
    std::vector<ObjectID> print_object_ids() const override;
    ApplyStatus         apply(const Model &model, DynamicPrintConfig config, bool extruder_applied = false) override;
    void                set_task(const TaskParams &params) override;
    void                process(long long *time_cost_with_cache = nullptr, bool use_cache = false) override;
    void                finalize() override;
    bool                is_step_done(SLAPrintObjectStep step) const;
    bool                finished() const override { return this->is_step_done(slaposSliceSupports) && this->Inherited::is_step_done(slapsRasterize); }

    const PrintObjects& objects() const { return m_objects; }
    const SLAPrintObject* get_print_object_by_model_object_id(ObjectID object_id) const {
        auto it = std::find_if(m_objects.begin(), m_objects.end(),
            [object_id](const SLAPrintObject* obj) { return obj->model_object()->id() == object_id; });
        return (it == m_objects.end()) ? nullptr : *it;
    }
    const SLAPrintObject* get_object(ObjectID object_id) const {
        auto it = std::find_if(m_objects.begin(), m_objects.end(),
            [object_id](const SLAPrintObject *obj) { return obj->id() == object_id; });
        return (it == m_objects.end()) ? nullptr : *it;
    }

    const SLAPrintConfig&       print_config() const { return m_print_config; }
    const SLAPrinterConfig&     printer_config() const { return m_printer_config; }
    const SLAMaterialConfig&    material_config() const { return m_material_config; }
    const SLAPrintObjectConfig& default_object_config() const { return m_default_object_config; }

    Vec3d                       relative_correction() const;

    Transform3d sla_trafo(const ModelObject &model_object) const;

	std::string                 output_filename(const std::string &filename_base = std::string()) const override;

    const SLAPrintStatistics&   print_statistics() const { return m_print_statistics; }

    StringObjectException validate(std::vector<StringObjectException> *    warnings          = nullptr,
                                   Polygons *                              collison_polygons = nullptr,
                                   std::vector<std::pair<Polygon, float>> *height_polygons   = nullptr) const override;

    class PrintLayer {
        coord_t m_level;

        std::vector<std::reference_wrapper<const SliceRecord>> m_slices;

        ExPolygons m_transformed_slices;

        template<class Container> void transformed_slices(Container&& c)
        {
            m_transformed_slices = std::forward<Container>(c);
        }

        friend class SLAPrint::Steps;

    public:

        explicit PrintLayer(coord_t lvl) : m_level(lvl) {}

        bool operator<(const PrintLayer& other) const {
            return m_level < other.m_level;
        }

        void add(const SliceRecord& sr) { m_slices.emplace_back(sr); }

        coord_t level() const { return m_level; }

        auto slices() const -> const decltype (m_slices)& { return m_slices; }

        const ExPolygons & transformed_slices() const {
            return m_transformed_slices;
        }
    };

    const std::vector<PrintLayer>& print_layers() const { return m_printer_input; }

    void set_printer(SLAArchive *archiver);

private:

    bool invalidate_step(SLAPrintStep st);

    bool invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys, bool &invalidate_all_model_objects);

    SLAPrintConfig                  m_print_config;
    SLAPrinterConfig                m_printer_config;
    SLAMaterialConfig               m_material_config;
    SLAPrintObjectConfig            m_default_object_config;

    PrintObjects                    m_objects;
    std::vector<bool>               m_stepmask;

    std::vector<PrintLayer>         m_printer_input;

    SLAArchive                     *m_printer = nullptr;

    SLAPrintStatistics              m_print_statistics;

    class StatusReporter
    {
        double m_st = 0;

    public:
        void operator()(SLAPrint &         p,
                        double             st,
                        const std::string &msg,
                        unsigned           flags = SlicingStatus::DEFAULT,
                        const std::string &logmsg = "");

        double status() const { return m_st; }
    } m_report_status;

	friend SLAPrintObject;
};

bool is_zero_elevation(const SLAPrintObjectConfig &c);

sla::SupportTreeConfig make_support_cfg(const SLAPrintObjectConfig& c);

sla::PadConfig::EmbedObject builtin_pad_cfg(const SLAPrintObjectConfig& c);

sla::PadConfig make_pad_cfg(const SLAPrintObjectConfig& c);

bool validate_pad(const indexed_triangle_set &pad, const sla::PadConfig &pcfg);


}

#endif
