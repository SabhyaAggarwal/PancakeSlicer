#ifndef slic3r_GLGizmoScale_hpp_
#define slic3r_GLGizmoScale_hpp_

#include "GLGizmoBase.hpp"
#include "GizmoObjectManipulation.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Geometry.hpp"
#include "slic3r/GUI/GLModel.hpp"
#include <utility>
#include <array>
#include <string>
#include <wx/event.h>
#include "libslic3r/Color.hpp"
#include "libslic3r/libslic3r.h"


namespace Slic3r {
namespace GUI {

class GLGizmoScale3D : public GLGizmoBase
{
    static const float Offset;

    struct StartingData
    {
        Vec3d scale;
        Vec3d drag_position;
        Vec3d constraint_position;
        Vec3d center{Vec3d::Zero()};
        Vec3d instance_center{Vec3d::Zero()};
        Vec3d plane_center;
        Vec3d plane_nromal;
        BoundingBoxf3 box;
        Vec3d pivots[6];
        Vec3d local_pivots[6];
        bool ctrl_down;

        StartingData() : scale(Vec3d::Ones()), drag_position(Vec3d::Zero()), ctrl_down(false) { for (int i = 0; i < 5; ++i) { pivots[i] = Vec3d::Zero(); } }
    };

    mutable BoundingBoxf3 m_bounding_box;
    Geometry::Transformation m_grabbers_tran;
    Vec3d                 m_center{Vec3d::Zero()};
    Vec3d                 m_instance_center{Vec3d::Zero()};
    Vec3d m_scale;
    Vec3d m_offset;
    double m_snap_step;
    StartingData m_starting;

    struct GrabberConnection
    {
        GLModel model;
        std::pair<unsigned int, unsigned int> grabber_indices;
        Vec3d old_v1{ Vec3d::Zero() };
        Vec3d old_v2{ Vec3d::Zero() };
    };
    std::array<GrabberConnection, 7> m_grabber_connections;

    GizmoObjectManipulation* m_object_manipulation;

public:
    GLGizmoScale3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, GizmoObjectManipulation* obj_manipulation);

    double get_snap_step(double step) const { return m_snap_step; }
    void set_snap_step(double step) { m_snap_step = step; }

    const Vec3d &get_scale();
    void set_scale(const Vec3d& scale) { m_starting.scale = scale; m_scale = scale; }

    const Vec3d& get_offset() const { return m_offset; }

    std::string get_tooltip() const override;

    bool on_mouse(const wxMouseEvent &mouse_event) override;

    void data_changed(bool is_serializing) override;
    void enable_ununiversal_scale(bool enable);
protected:
    virtual bool on_init() override;
    virtual std::string on_get_name() const override;
    virtual bool on_is_activable() const override;
    virtual void on_set_state() override;
    virtual void on_start_dragging() override;
    virtual void on_stop_dragging() override;
    virtual void on_dragging(const UpdateData& data) override;
    virtual void on_render() override;
    virtual void on_register_raycasters_for_picking() override;
    virtual void on_unregister_raycasters_for_picking() override;
    virtual void on_render_input_window(float x, float y, float bottom_limit) override;

private:
    void render_grabbers_connection(unsigned int id_1, unsigned int id_2, const ColorRGBA& color);

    void do_scale_along_axis(Axis axis, const UpdateData& data);
    void do_scale_uniform(const UpdateData& data);

    double calc_ratio(const UpdateData& data) const;
    void   update_grabbers_data();
    void   change_cs_by_selection();
private:
    int m_last_selected_obejct_idx, m_last_selected_volume_idx;
};


}
}

#endif
