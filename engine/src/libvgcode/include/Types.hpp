///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/ libvgcode is released under the terms of the AGPLv3 or higher
#ifndef VGCODE_TYPES_HPP
#define VGCODE_TYPES_HPP

#include <cstddef>
#define VGCODE_ENABLE_COG_AND_TOOL_MARKERS 0

#include <array>
#include <vector>
#include <cstdint>

namespace libvgcode {

static constexpr float PI = 3.141592f;

static constexpr float DEFAULT_TRAVELS_RADIUS_MM = 0.1f;
static constexpr float MIN_TRAVELS_RADIUS_MM = 0.05f;
static constexpr float MAX_TRAVELS_RADIUS_MM = 1.0f;

static constexpr float DEFAULT_WIPES_RADIUS_MM = 0.1f;
static constexpr float MIN_WIPES_RADIUS_MM = 0.05f;
static constexpr float MAX_WIPES_RADIUS_MM = 1.0f;

using Vec3 = std::array<float, 3>;
using Vec4 = std::array<float, 4>;

using Mat4x4 = std::array<float, 16>;

using Color = std::array<uint8_t, 3>;

using Palette = std::vector<Color>;

using AABox = std::array<Vec3, 2>;

using Interval = std::array<std::size_t, 2>;

enum class EViewType : uint8_t
{
    Summary,
    FeatureType,
    ColorPrint,
    Speed,
    ActualSpeed,
    Height,
    Width,
    VolumetricFlowRate,
    ActualVolumetricFlowRate,
    LayerTimeLinear,
    LayerTimeLogarithmic,
    FanSpeed,
    Temperature,
    PressureAdvance,
    Acceleration,
    Jerk,
    Tool,
    Clearance,
    Nonplanar,
    COUNT
};

static constexpr std::size_t VIEW_TYPES_COUNT = static_cast<std::size_t>(EViewType::COUNT);

enum class ENonplanarColor : uint8_t
{
    Shell,
    Band,
    BeadHeight,
    Slope,
    COUNT
};

enum class EMoveType : uint8_t
{
    Noop,
    Retract,
    Unretract,
    Seam,
    ToolChange,
    ColorChange,
    PausePrint,
    CustomGCode,
    Travel,
    Wipe,
    Extrude,
    COUNT
};

static constexpr std::size_t MOVE_TYPES_COUNT = static_cast<std::size_t>(EMoveType::COUNT);

enum class EGCodeExtrusionRole : uint8_t
{
	  None,
	  Perimeter,
	  ExternalPerimeter,
	  OverhangPerimeter,
	  InternalInfill,
	  SolidInfill,
	  TopSolidInfill,
	  Ironing,
	  BridgeInfill,
	  GapFill,
	  Skirt,
	  SupportMaterial,
	  SupportMaterialInterface,
	  WipeTower,
	  Custom,
      BottomSurface,
      InternalBridgeInfill,
      Brim,
      SupportTransition,
      Mixed,
    COUNT
};

static constexpr std::size_t GCODE_EXTRUSION_ROLES_COUNT = static_cast<std::size_t>(EGCodeExtrusionRole::COUNT);

enum class EOptionType : uint8_t
{
    Travels,
    Wipes,
    Retractions,
    Unretractions,
    Seams,
    ToolChanges,
    ColorChanges,
    PausePrints,
    CustomGCodes,
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    CenterOfGravity,
    ToolMarker,
#endif
    COUNT
};

static constexpr std::size_t OPTION_TYPES_COUNT = static_cast<std::size_t>(EOptionType::COUNT);

enum class ETimeMode : uint8_t
{
    Normal,
    Stealth,
    COUNT
};

static constexpr std::size_t TIME_MODES_COUNT = static_cast<std::size_t>(ETimeMode::COUNT);

enum class EColorRangeType : uint8_t
{
    Linear,
    Logarithmic,
    COUNT
};

static constexpr std::size_t COLOR_RANGE_TYPES_COUNT = static_cast<std::size_t>(EColorRangeType::COUNT);

static const Color DUMMY_COLOR{ 64, 64, 64 };

extern EOptionType move_type_to_option(EMoveType type);

extern Color lerp(const Color& c1, const Color& c2, float t);

}

#endif
