#pragma once

#include "KeyChord.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Slic3r {

class AppConfig;

namespace GUI {

enum class ShortcutContext : uint8_t { Global, Plater, Preview, ObjectList, Painting, Count };

constexpr uint8_t context_bit(ShortcutContext context) { return uint8_t(1u << unsigned(context)); }

enum class Shortcut : uint8_t {
    NewProject, OpenProject, SaveProject, SaveProjectAs, ImportModel, Publish3mf,
    SlicePlate, ExportSlicedFile, PrintPlate, PrintHostQueue,
    SelectAll, SelectAllPlates,
    Undo, Redo, Cut, Copy, Paste, DeleteSelected, DeleteAll, CloneSelected,
    AddInstance, RemoveInstance, TogglePrintable, ToggleAutoDrop,
    Arrange, ArrangePlate, Orient, OrientPlate,
    MoveSelectionLeft, MoveSelectionRight, MoveSelectionUp, MoveSelectionDown, RotateSelectionLeft, RotateSelectionRight,
    GizmoMove, GizmoRotate, GizmoScale, GizmoFlatten, GizmoCut, GizmoMeshBoolean, GizmoFdmSupports, GizmoSeam, GizmoFuzzySkin, GizmoNonplanar,
    GizmoMmuSegmentation, GizmoEmboss, GizmoMeasure, GizmoAssembly, GizmoBrimEars, GizmoDisplacement,
    GoToLayer, LayerSliderUp, LayerSliderDown, MovesSliderLeft, MovesSliderRight, MovesSliderStart, MovesSliderEnd,
    PaintToolCircle, PaintToolSphere, PaintToolFill, PaintToolGapFill, PaintToolTriangle, PaintToolHeightRange,
    ViewDefault, ViewTop, ViewBottom, ViewFront, ViewRear, ViewLeft, ViewRight, ViewPlate, ZoomIn, ZoomOut, Mouse3DSettings,
    ShowLabels, ShowWireframe, ToggleGcodeWindow, ToggleOneLayerMode, ToggleBeltRawGcode,
    Preferences, Search, SwitchView, CollapseSidebar, ReloadDevicePage, KeyboardShortcuts,
    SpeedDial,
    Count
};

struct ShortcutInfo
{
    Shortcut    id;
    const char* key;
    const char* name;
    uint8_t     contexts;
    KeyChord    default_chord;
    bool        repeatable;
    bool        modifier_variants;
};

enum class ShortcutSection : uint8_t {
    Project, SlicingAndPrinting, Selection, Editing, Objects, Placement, Gizmos, Sliders, PaintingTools, Camera, Display, Application, SpeedDial,
    Count
};

const ShortcutInfo&   shortcut_info(Shortcut shortcut);
ShortcutSection       shortcut_section(Shortcut shortcut);
const char*           section_name(ShortcutSection section);
std::vector<Shortcut> shortcuts_in(ShortcutContext context);
class ShortcutRegistry
{
public:
    ShortcutRegistry();

    KeyChord binding(Shortcut shortcut) const;
    bool     is_customized(Shortcut shortcut) const;

    std::string display(Shortcut shortcut) const;
    std::string with_key(const std::string& text, Shortcut shortcut) const;
    std::string accelerator(Shortcut shortcut) const;
    std::optional<Shortcut> lookup(ShortcutContext context, const KeyChord& chord) const;

    struct Match
    {
        Shortcut shortcut;
        int      step_modifiers;
    };
    std::optional<Match> match(ShortcutContext context, const KeyChord& chord) const;

    std::vector<Shortcut> conflicts(Shortcut shortcut, const KeyChord& chord) const;

    std::optional<Shortcut> step_owner(Shortcut shortcut, const KeyChord& chord) const;

    void bind(Shortcut shortcut, const KeyChord& chord);
    void reset(Shortcut shortcut);
    void reset_all();

    void load(const AppConfig& config);
    void save(AppConfig& config) const;

private:
    void rebuild_index();

    std::array<std::optional<KeyChord>, size_t(Shortcut::Count)>         m_overrides;
    std::unordered_map<KeyChord, std::vector<Shortcut>, KeyChordHash> m_index;
};

}
}
