#ifndef slic3r_GLGizmoAssembly_hpp_
#define slic3r_GLGizmoAssembly_hpp_

#include "GLGizmoMeasure.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoBase.hpp"
#include <string>

namespace Slic3r {

namespace GUI {
class GLGizmoAssembly : public GLGizmoMeasure
{

public:
    GLGizmoAssembly(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    bool wants_enter_leave_snapshots() const override { return true; }
    std::string get_gizmo_entering_text() const override { return _u8L("Entering Assembly gizmo"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leaving Assembly gizmo"); }
protected:
    bool on_init() override;
    std::string on_get_name() const override;
    bool on_is_activable() const override;
    virtual void on_render_input_window(float x, float y, float bottom_limit) override;

    void render_input_window_warning(bool same_model_object) override;
    bool render_assembly_mode_combo(double label_width, float item_width);

    void switch_to_mode(AssemblyMode new_mode);
};

}
}

#endif
