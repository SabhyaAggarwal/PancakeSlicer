#ifndef slic3r_GUI_GLGizmosCommon_hpp_
#define slic3r_GUI_GLGizmosCommon_hpp_

#include "libslic3r/TriangleMesh.hpp"
#include <cassert>
#include <cstddef>
#include "libslic3r/Point.hpp"
#include "libslic3r/Geometry.hpp"
#include <memory>
#include <map>
#include <vector>
#include <utility>

#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/MeshUtils.hpp"

namespace Slic3r {

class ModelObject;
class ModelInstance;
class SLAPrintObject;
class ModelVolume;

namespace GUI {

class GLCanvas3D;

enum class SLAGizmoEventType : unsigned char {
    LeftDown = 1,
    LeftUp,
    RightDown,
    RightUp,
    Dragging,
    Delete,
    SelectAll,
    CtrlDown,
    CtrlUp,
    ShiftDown,
    ShiftUp,
    AltUp,
    Escape,
    ApplyChanges,
    DiscardChanges,
    AutomaticGeneration,
    ManualEditing,
    MouseWheelUp,
    MouseWheelDown,
    ResetClippingPlane,
    Moving
};



class CommonGizmosDataBase;
namespace CommonGizmosDataObjects {
    class SelectionInfo;
    class InstancesHider;
    class HollowedMesh;
    class Raycaster;
    class ObjectClipper;
    class SupportsClipper;
}

enum class CommonGizmosDataID {
    None                 = 0,
    SelectionInfo        = 1 << 0,
    InstancesHider       = 1 << 1,
    Raycaster            = 1 << 3,
    ObjectClipper        = 1 << 4,

};


class CommonGizmosDataPool {
public:
    explicit CommonGizmosDataPool(GLCanvas3D* canvas);

    void update(CommonGizmosDataID required);

    CommonGizmosDataObjects::SelectionInfo* selection_info() const;
    CommonGizmosDataObjects::InstancesHider* instances_hider() const;
    CommonGizmosDataObjects::Raycaster *  raycaster_ptr();
    CommonGizmosDataObjects::Raycaster* raycaster() const;
    CommonGizmosDataObjects::ObjectClipper* object_clipper() const;


    GLCanvas3D* get_canvas() const { return m_canvas; }

private:
    std::map<CommonGizmosDataID, std::unique_ptr<CommonGizmosDataBase>> m_data;
    GLCanvas3D* m_canvas;

#ifndef NDEBUG
    bool check_dependencies(CommonGizmosDataID required) const;
#endif
};





class CommonGizmosDataBase {
public:
    explicit CommonGizmosDataBase(CommonGizmosDataPool* cgdp)
        : m_common{cgdp} {}
    virtual ~CommonGizmosDataBase() {}

    void update() { on_update(); m_is_valid = true; }

    void release() { on_release(); m_is_valid = false; }

    bool is_valid() const { return m_is_valid; }

#ifndef NDEBUG
    virtual CommonGizmosDataID get_dependencies() const { return CommonGizmosDataID::None; }
#endif

protected:
    virtual void on_release() = 0;
    virtual void on_update() = 0;
    CommonGizmosDataPool* get_pool() const { return m_common; }

private:
    bool m_is_valid = false;
    CommonGizmosDataPool* m_common = nullptr;
};



namespace CommonGizmosDataObjects
{

class SelectionInfo : public CommonGizmosDataBase
{
public:
    explicit SelectionInfo(CommonGizmosDataPool* cgdp)
        : CommonGizmosDataBase(cgdp) {}

    ModelObject* model_object() const { return m_model_object; }
    int get_active_instance() const;
    float get_sla_shift() const { return m_z_shift; }

protected:
    void on_update() override;
    void on_release() override;

private:
    ModelObject* m_model_object = nullptr;
    float m_z_shift = 0.f;
};



class InstancesHider : public CommonGizmosDataBase
{
public:
    explicit InstancesHider(CommonGizmosDataPool* cgdp)
        : CommonGizmosDataBase(cgdp) {}
#ifndef NDEBUG
    CommonGizmosDataID get_dependencies() const override { return CommonGizmosDataID::SelectionInfo; }
#endif

    void render_cut() const;

protected:
    void on_update() override;
    void on_release() override;

private:
    std::vector<const TriangleMesh*> m_old_meshes;
    std::vector<std::unique_ptr<MeshClipper>> m_clippers;
};



class Raycaster : public CommonGizmosDataBase
{
public:
    explicit Raycaster(CommonGizmosDataPool* cgdp)
        : CommonGizmosDataBase(cgdp) {}
#ifndef NDEBUG
    CommonGizmosDataID get_dependencies() const override { return CommonGizmosDataID::SelectionInfo; }
#endif

    const MeshRaycaster* raycaster() const { assert(m_raycasters.size() == 1); return m_raycasters.front().get(); }
    std::vector<const MeshRaycaster*> raycasters() const;
    void  set_only_support_model_part_flag(bool);

protected:
    void on_update() override;
    void on_release() override;

private:
    std::vector<std::unique_ptr<MeshRaycaster>> m_raycasters;
    std::vector<const TriangleMesh*> m_old_meshes;
    bool  m_only_support_model_part{true};
};



class ObjectClipper : public CommonGizmosDataBase
{
public:
    explicit ObjectClipper(CommonGizmosDataPool* cgdp)
        : CommonGizmosDataBase(cgdp) {}
#ifndef NDEBUG
    CommonGizmosDataID get_dependencies() const override { return CommonGizmosDataID::SelectionInfo; }
#endif
    double get_position() const { return m_clp_ratio; }
    void set_position_to_init_layer();
    const ClippingPlane* get_clipping_plane(bool ignore_hide_clipped = false) const;
    void render_cut(const std::vector<size_t>* ignore_idxs = nullptr) const;
    void set_position_by_ratio(double pos, const Vec3d& normal);
    void set_range_and_pos(const Vec3d& cpl_normal, double cpl_offset, double pos);
    void set_behavior(bool hide_clipped, bool fill_cut, double contour_width);
    
    int get_number_of_contours() const;
    std::vector<Vec3d> point_per_contour() const;

    int is_projection_inside_cut(const Vec3d& point_in) const;
    bool has_valid_contour() const;


protected:
    void on_update() override;
    void on_release() override;

private:
    std::vector<const TriangleMesh*> m_old_meshes;
    std::vector<std::pair<std::unique_ptr<MeshClipper>, Geometry::Transformation>> m_clippers;
    std::unique_ptr<ClippingPlane> m_clp;
    double m_clp_ratio = 0.;
    double m_active_inst_bb_radius = 0.;
    bool m_hide_clipped = true;
};

}


}
}


#endif
