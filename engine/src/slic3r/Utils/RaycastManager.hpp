#ifndef slic3r_RaycastManager_hpp_
#define slic3r_RaycastManager_hpp_

#include <cstddef>
#include <Eigen/Core>
#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include "libslic3r/AABBMesh.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/ObjectID.hpp"
#include "libslic3r/Model.hpp"

namespace Slic3r::GUI{

class RaycastManager
{
public: 

    using Mesh = std::pair<size_t, std::unique_ptr<AABBMesh> >;
    using Meshes = std::vector<Mesh>;
    
    using TrKey = std::pair<size_t, size_t>;
    using TrItem = std::pair<TrKey, Transform3d>;
    using TrItems = std::vector<TrItem>;

    class ISkip{
    public:
        virtual ~ISkip() = default;

        virtual bool skip(const size_t &model_volume_id) const { return false; }
    };

    template<typename T> 
    struct SurfacePoint {
        using Vec3 = Eigen::Matrix<T, 3, 1, Eigen::DontAlign>;
        Vec3 position = Vec3::Zero();
        Vec3 normal   = Vec3::UnitZ();
    };

    struct Hit : public SurfacePoint<double>
    {
        TrKey tr_key;
        double squared_distance;
    };    

    struct ClosePoint
    {
        TrKey tr_key;
        Vec3d  point;
        double squared_distance;
    };

private:
    Meshes m_meshes;

    TrItems m_transformations;

public:

    void actualize(const ModelObject &object, const ISkip *skip = nullptr, Meshes *meshes = nullptr);
    void actualize(const ModelInstance &instance, const ISkip *skip = nullptr, Meshes* meshes = nullptr);

    class SkipVolume: public ISkip
    {
        size_t volume_id;
    public:
        SkipVolume(size_t volume_id) : volume_id(volume_id) {}
        bool skip(const size_t &model_volume_id) const override { return model_volume_id == volume_id; }
    };

    class AllowVolumes: public ISkip
    {
        std::vector<size_t> allowed_id;
    public:
        AllowVolumes(std::vector<size_t> allowed_id) : allowed_id(allowed_id) {}
        bool skip(const size_t &model_volume_id) const override {
            auto it = std::find(allowed_id.begin(), allowed_id.end(), model_volume_id);
            return it == allowed_id.end(); 
        }
    };

    std::optional<Hit> first_hit(const Vec3d &point, const Vec3d &direction, const ISkip *skip = nullptr) const;

    std::optional<Hit> closest_hit(const Vec3d &point, const Vec3d &direction, const ISkip *skip = nullptr) const;

    std::optional<ClosePoint> closest(const Vec3d &point, const ISkip *skip = nullptr) const;

    Transform3d get_transformation(const TrKey &tr_key) const;
};

class GLCanvas3D;
RaycastManager::Meshes create_meshes(GLCanvas3D &canvas, const RaycastManager::AllowVolumes &condition);

struct Camera;
std::optional<RaycastManager::Hit> ray_from_camera(const RaycastManager        &raycaster,
                                                   const Vec2d                 &mouse_pos,
                                                   const Camera                &camera,
                                                   const RaycastManager::ISkip *skip);

RaycastManager::AllowVolumes create_condition(const ModelVolumePtrs &volumes, const ObjectID &disallowed_volume_id);

}

#endif
