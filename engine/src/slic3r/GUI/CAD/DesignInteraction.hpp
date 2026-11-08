#ifndef slic3r_GUI_DesignInteraction_hpp_
#define slic3r_GUI_DesignInteraction_hpp_

namespace Slic3r { namespace GUI {

enum class CadLevel : int {
    Idle      = 0,
    Tool      = 1,
    Gesture   = 2,
    Transient = 3,
};

struct CadInteractionState {
    bool value_field_open{false};
    bool gesture_active{false};
    bool tool_armed{false};
    bool has_selection{false};
};

constexpr CadLevel cad_escape_level(const CadInteractionState& s)
{
    if (s.value_field_open) return CadLevel::Transient;
    if (s.gesture_active)   return CadLevel::Gesture;
    if (s.tool_armed)       return CadLevel::Tool;
    return CadLevel::Idle;
}

static_assert(cad_escape_level({true, true, true, true})     == CadLevel::Transient, "value field is deepest");
static_assert(cad_escape_level({false, true, true, true})    == CadLevel::Gesture,   "gesture beats tool");
static_assert(cad_escape_level({false, false, true, true})   == CadLevel::Tool,      "tool beats idle");
static_assert(cad_escape_level({false, false, false, true})  == CadLevel::Idle,      "selection is idle-level");
static_assert(cad_escape_level({false, false, false, false}) == CadLevel::Idle,      "empty is idle");

inline constexpr int kCadRightClickDriftPx = 3;

}}

#endif
