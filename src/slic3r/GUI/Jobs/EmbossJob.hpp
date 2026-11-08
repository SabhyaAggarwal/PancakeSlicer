#ifndef slic3r_EmbossJob_hpp_
#define slic3r_EmbossJob_hpp_

#include <atomic>
#include <exception>
#include <memory>
#include <optional>
#include "slic3r/GUI/3DScene.hpp"
#include <string>
#include <libslic3r/Emboss.hpp>
#include <libslic3r/EmbossShape.hpp>
#include <utility>
#include <vector>
#include "libslic3r/Point.hpp"
#include "libslic3r/ObjectID.hpp"

#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/TextLines.hpp"

#include "Job.hpp"

namespace Slic3r {
class TriangleMesh;
class ModelVolume;
enum class ModelVolumeType : int;
class BuildVolume;
namespace GUI {
class RaycastManager;
class Plater;
class GLCanvas3D;
class Worker;
class Selection;
}}

namespace Slic3r::GUI::Emboss {

class DataBase
{
public:
    DataBase(const std::string& volume_name, std::shared_ptr<std::atomic<bool>> cancel) 
        : volume_name(volume_name), cancel(std::move(cancel)) {}
    DataBase(const std::string& volume_name, std::shared_ptr<std::atomic<bool>> cancel, EmbossShape&& shape)
        : volume_name(volume_name), cancel(std::move(cancel)), shape(std::move(shape)){}
    DataBase(DataBase &&) = default;
    virtual ~DataBase() = default;

    virtual EmbossShape& create_shape() { return shape; };

    virtual void write(ModelVolume &volume) const;

    bool is_outside = true;

    Slic3r::Emboss::TextLines text_lines = {};

    std::optional<float> from_surface;
        
    std::string volume_name;

    std::shared_ptr<std::atomic<bool>> cancel;

    EmbossShape shape;
};

struct DataCreateVolume : public DataBase
{
    ModelVolumeType volume_type;

    ObjectID object_id;

    Transform3d trmat;
};
using DataBasePtr = std::unique_ptr<DataBase>;

struct DataUpdate
{
    DataBasePtr base;

    ObjectID volume_id;

    bool make_snapshot;
};

class UpdateJob : public Job
{
    DataUpdate   m_input;
    TriangleMesh m_result;

public:
    explicit UpdateJob(DataUpdate &&input);

    void process(Ctl &ctl) override;

    void finalize(bool canceled, std::exception_ptr &eptr) override;

    static void update_volume(ModelVolume *volume, TriangleMesh &&mesh, const DataBase &base);
};

struct SurfaceVolumeData
{
    Transform3d transform;

    struct ModelSource
    {
        std::shared_ptr<const TriangleMesh> mesh;
        Transform3d tr;
    };
    using ModelSources = std::vector<ModelSource>;
    ModelSources sources;
};

struct UpdateSurfaceVolumeData : public DataUpdate, public SurfaceVolumeData{};

class UpdateSurfaceVolumeJob : public Job
{
    UpdateSurfaceVolumeData m_input;
    TriangleMesh            m_result;

public:
    explicit UpdateSurfaceVolumeJob(UpdateSurfaceVolumeData &&input);
    void process(Ctl &ctl) override;
    void finalize(bool canceled, std::exception_ptr &eptr) override;
};

SurfaceVolumeData::ModelSources create_volume_sources(const ModelVolume &volume);

struct CreateVolumeParams
{
    GLCanvas3D &canvas;

    const Camera &camera;

    const BuildVolume &build_volume;

    Worker &worker;

    ModelVolumeType volume_type;

    RaycastManager &raycaster;

    unsigned char gizmo;
    const GLVolume *gl_volume;

    std::optional<float> distance = {};

    std::optional<float> angle = {};
};

bool start_create_volume(CreateVolumeParams &input, DataBasePtr data, const Vec2d &mouse_pos);

bool start_create_volume_without_position(CreateVolumeParams &input, DataBasePtr data);

bool start_update_volume(DataUpdate &&data, const ModelVolume &volume, const Selection &selection, RaycastManager &raycaster);

}

#endif
