#ifndef slic3r_GLGizmoFlatten_hpp_
#define slic3r_GLGizmoFlatten_hpp_

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/GLModel.hpp"
#include "slic3r/GUI/MeshUtils.hpp"
#include <vector>
#include "libslic3r/Point.hpp"
#include <memory>
#include "slic3r/GUI/SceneRaycaster.hpp"
#include <string>
#include <wx/event.h>

namespace Slic3r {

enum class ModelVolumeType : int;


namespace GUI {


class GLGizmoFlatten : public GLGizmoBase
{

private:

    struct PlaneData {
        std::vector<Vec3d> vertices;
        PickingModel vbo;
        Vec3d normal;
        float area;
        int picking_id{ -1 };
    };

    std::vector<Transform3d> m_volumes_matrices;
    std::vector<ModelVolumeType> m_volumes_types;
    Vec3d m_first_instance_scale;
    Vec3d m_first_instance_mirror;

    std::vector<PlaneData> m_planes;
    std::vector<std::shared_ptr<SceneRaycasterItem>> m_planes_casters;
    const ModelObject* m_old_model_object = nullptr;
    int                m_old_instance_id{ -1 };

    void update_planes();
    bool is_plane_update_necessary() const;

public:
    GLGizmoFlatten(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    void set_flattening_data(const ModelObject* model_object, int instance_id);
        
    bool on_mouse(const wxMouseEvent &mouse_event) override;

    void data_changed(bool is_serializing) override;
protected:
    bool on_init() override;
    std::string on_get_name() const override;
    bool on_is_activable() const override;
    void on_render() override;
    void on_register_raycasters_for_picking() override;
    void on_unregister_raycasters_for_picking() override;
    void on_set_state() override;
    CommonGizmosDataID on_get_requirements() const override;
};

}
}

#endif
