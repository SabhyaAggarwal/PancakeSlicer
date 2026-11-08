#ifndef SLIC3R_TEST_HELPERS_HPP
#define SLIC3R_TEST_HELPERS_HPP

#include "libslic3r/Config.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "libslic3r/libslic3r.h"
#include <cstddef>
#include "libslic3r/PrintConfig.hpp"
#include <initializer_list>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace Slic3r {
struct GCodeProcessorResult;
namespace Test {

constexpr double MM_PER_MIN = 60.0;

template <typename T>
bool _equiv(const T& a, const T& b) { return std::abs(a - b) < EPSILON; }

template <typename T>
bool _equiv(const T& a, const T& b, double epsilon) { return abs(a - b) < epsilon; }

enum class TestMesh {
    A,
    L,
    V,
    _40x10,
    sphere_50mm,
    bridge,
    bridge_with_hole,
    cube_with_concave_hole,
    cube_with_hole,
    gt2_teeth,
    ipadstand,
    overhang,
    pyramid,
    sloping_hole,
    slopy_cube,
    small_dorito,
    step,
    two_hollow_squares
};

struct TestMeshHash {
    std::size_t operator()(TestMesh tm) const {
        return static_cast<std::size_t>(tm);
    }
};

extern const std::unordered_map<TestMesh, const char*, TestMeshHash> mesh_names;

TriangleMesh mesh(TestMesh m);
TriangleMesh mesh(TestMesh m, Vec3d translate, Vec3d scale = Vec3d(1.0, 1.0, 1.0));
TriangleMesh mesh(TestMesh m, Vec3d translate, double scale = 1.0);

inline TriangleMesh cube(double size) { return make_cube(size, size, size); }

Slic3r::Model model(const std::string& model_name, TriangleMesh&& _mesh);

DynamicPrintConfig multifilament_config(unsigned int filaments,
    std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> extra = {});

void size_flush_to_nozzles(DynamicPrintConfig &config);

void init_print(std::vector<TriangleMesh> &&meshes, Slic3r::Print &print, Slic3r::Model &model, const DynamicPrintConfig &config_in,
    const std::vector<std::vector<Slic3r::ConfigBase::SetDeserializeItem>> *per_object_overrides = nullptr, bool arrange = true,
    size_t instances = 1);
void init_print(std::initializer_list<TestMesh> meshes, Slic3r::Print &print, Slic3r::Model &model, const Slic3r::DynamicPrintConfig &config_in = Slic3r::DynamicPrintConfig::full_print_config());
void init_print(std::initializer_list<TriangleMesh> meshes, Slic3r::Print &print, Slic3r::Model &model, const Slic3r::DynamicPrintConfig &config_in = Slic3r::DynamicPrintConfig::full_print_config());
void init_print(std::initializer_list<TestMesh> meshes, Slic3r::Print &print, Slic3r::Model &model, std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> config_items);
void init_print(std::initializer_list<TriangleMesh> meshes, Slic3r::Print &print, Slic3r::Model &model, std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> config_items);

void init_and_process_print(std::initializer_list<TestMesh> meshes, Slic3r::Print &print, const DynamicPrintConfig& config);
void init_and_process_print(std::initializer_list<TriangleMesh> meshes, Slic3r::Print &print, const DynamicPrintConfig& config);
void init_and_process_print(std::initializer_list<TestMesh> meshes, Slic3r::Print &print, std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> config_items);
void init_and_process_print(std::initializer_list<TriangleMesh> meshes, Slic3r::Print &print, std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> config_items);

std::string gcode(Print& print, GCodeProcessorResult* result = nullptr);

std::string slice(std::initializer_list<TestMesh> meshes, const DynamicPrintConfig &config);
std::string slice(std::initializer_list<TriangleMesh> meshes, const DynamicPrintConfig &config);
std::string slice(std::initializer_list<TestMesh> meshes, std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> config_items);
std::string slice(std::initializer_list<TriangleMesh> meshes, std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> config_items);

std::string slice_with_object_overrides(std::initializer_list<TriangleMesh> meshes, const DynamicPrintConfig &config,
    const std::vector<std::vector<Slic3r::ConfigBase::SetDeserializeItem>> &per_object_overrides);

std::string slice_two_cubes_arranged(std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> config_items);

void place_two_cubes_apart(double gap, std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> config_items,
    Slic3r::Print &print, Slic3r::Model &model);
std::string slice_two_cubes_apart(double gap, std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> config_items);

void place_two_cube_instances_apart(double gap, std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> config_items,
    Slic3r::Print &print, Slic3r::Model &model);

std::set<double> layers_with_role(const std::string &gcode, const std::string &role);

double max_z(const std::string &gcode);

int role_passes(const std::string &gcode, const std::string &role);

std::vector<std::string> role_sequence(const std::string &gcode, const std::vector<std::string> &roles);

} }

#endif
