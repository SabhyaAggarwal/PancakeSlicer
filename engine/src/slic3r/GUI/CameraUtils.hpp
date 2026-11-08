#ifndef slic3r_CameraUtils_hpp_
#define slic3r_CameraUtils_hpp_

#include "Camera.hpp"
#include "libslic3r/Point.hpp"
#include <vector>
#include "libslic3r/Polygon.hpp"
namespace Slic3r {
class GLVolume;
}
	
namespace Slic3r::GUI {
class CameraUtils
{
public:
    CameraUtils() = delete;
	static Points project(const Camera& camera, const std::vector<Vec3d> &points);
	static Slic3r::Point project(const Camera& camera, const Vec3d &point);

	static Polygon create_hull2d(const Camera &camera, const GLVolume &volume);
	
    static void ray_from_screen_pos(const Camera &camera, const Vec2d &position, Vec3d &point, Vec3d &direction); 
    static void ray_from_ortho_screen_pos(const Camera &camera, const Vec2d &position, Vec3d &point, Vec3d &direction);
    static void ray_from_persp_screen_pos(const Camera &camera, const Vec2d &position, Vec3d &point, Vec3d &direction);

    static Vec2d get_z0_position(const Camera &camera, const Vec2d &coor);

    static Vec3d screen_point(const Camera &camera, const Vec2d &position);

};
}

#endif
