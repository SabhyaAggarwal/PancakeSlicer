#include <catch2/catch_all.hpp>
#include "libslic3r/Point.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include <utility>
#include <vector>
#include "libslic3r/libslic3r.h"
#include <cstddef>

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "libslic3r/LayOnFace.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/BoundingBox.hpp"

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

namespace {

void add_box(ModelObject &object, const Vec3d &size, const Vec3d &origin = Vec3d::Zero())
{
    TriangleMesh mesh = make_cube(size.x(), size.y(), size.z());
    mesh.translate(origin.cast<float>());
    object.add_volume(std::move(mesh), ModelVolumeType::MODEL_PART, false);
}

ModelObject &add_box_object(Model &model, const Vec3d &size)
{
    ModelObject *object = model.add_object();
    add_box(*object, size);
    object->add_instance();
    return *object;
}

ModelObject &add_ribbed_plate(Model &model)
{
    ModelObject *object = model.add_object();
    add_box(*object, { 30, 30, 2 });
    for (double x : { 5., 14.5, 24. })
        add_box(*object, { 1, 30, 20 }, { x, 0, 2 });
    object->add_instance();
    return *object;
}

std::vector<LayOnFacePlane> instance_planes(const ModelObject &object)
{
    return lay_on_face_planes(object, object.instances.front()->get_matrix_no_offset());
}

void lay_on_largest_face(ModelObject &object)
{
    const std::vector<LayOnFacePlane> planes = instance_planes(object);
    const int                         idx    = find_largest_plane(planes);
    REQUIRE(idx >= 0);
    lay_on_face(object, 0, planes[idx].normal);
}

void check_size(const ModelObject &object, const Vec3d &expected)
{
    const Vec3d size = object.instance_bounding_box(0).size();
    CHECK_THAT(size.x(), WithinAbs(expected.x(), 1e-3));
    CHECK_THAT(size.y(), WithinAbs(expected.y(), 1e-3));
    CHECK_THAT(size.z(), WithinAbs(expected.z(), 1e-3));
}

void check_on_bed(const ModelObject &object) { CHECK_THAT(object.instance_bounding_box(0).min.z(), WithinAbs(0., 1e-3)); }

}

TEST_CASE("A tilted box is laid on its largest face and dropped onto the bed", "[LayOnFace]")
{
    Model        model;
    ModelObject &box = add_box_object(model, { 40, 20, 10 });
    box.instances.front()->set_rotation({ 0.3, 0.5, 0.2 });
    box.instances.front()->set_offset({ 0, 0, 50 });
    REQUIRE(box.instance_bounding_box(0).size().z() > 11.);

    const std::vector<LayOnFacePlane> planes = instance_planes(box);
    REQUIRE(planes.size() == 6);
    CHECK_THAT(planes.front().area, WithinAbs(40. * 20., 1e-2));

    lay_on_largest_face(box);
    CHECK_THAT(box.instance_bounding_box(0).size().z(), WithinAbs(10., 1e-3));
    check_on_bed(box);
}

TEST_CASE("A box lying on one of its equally large faces is not flipped", "[LayOnFace]")
{
    const double rotation_x = GENERATE(0., PI);
    Model        model;
    ModelObject &box = add_box_object(model, { 40, 20, 10 });
    box.instances.front()->set_rotation({ rotation_x, 0, 0 });
    const Transform3d before = box.instances.front()->get_matrix_no_offset();

    const std::vector<LayOnFacePlane> planes = instance_planes(box);
    const int                         idx    = find_largest_plane(planes);
    REQUIRE(idx >= 0);
    CHECK_THAT(planes[idx].normal.z(), WithinAbs(rotation_x == 0. ? -1. : 1., 1e-6));

    lay_on_face(box, 0, planes[idx].normal);
    CHECK(box.instances.front()->get_matrix_no_offset().isApprox(before, 1e-9));
}

TEST_CASE("Faces are chosen from the orientation left by an earlier part rotation", "[LayOnFace]")
{
    Model        model;
    ModelObject &box = add_box_object(model, { 40, 20, 10 });
    box.rotate(PI / 2., X);
    check_size(box, { 40, 10, 20 });

    SECTION("the largest face") {
        lay_on_largest_face(box);
        check_size(box, { 40, 20, 10 });
        check_on_bed(box);
    }

    SECTION("the face pointing along +X") {
        const std::vector<LayOnFacePlane> planes = instance_planes(box);
        const int                         idx    = find_plane_by_normal(planes, { 1, 0, 0 });
        REQUIRE(idx >= 0);
        CHECK_THAT(planes[idx].normal.x(), WithinAbs(1., 1e-6));
        lay_on_face(box, 0, planes[idx].normal);
        check_size(box, { 20, 10, 40 });
        check_on_bed(box);
    }
}

TEST_CASE("Objects are laid on their own faces independently", "[LayOnFace]")
{
    Model model;
    ModelObject &standing = add_box_object(model, { 40, 20, 10 });
    standing.instances.front()->set_rotation({ 0, PI / 2., 0 });
    ModelObject &on_edge = add_box_object(model, { 30, 20, 5 });
    on_edge.rotate(PI / 2., X);
    on_edge.instances.front()->set_offset({ 100, 0, 30 });
    check_size(standing, { 10, 20, 40 });
    check_size(on_edge, { 30, 5, 20 });

    for (ModelObject *object : model.objects)
        lay_on_largest_face(*object);

    check_size(standing, { 40, 20, 10 });
    check_on_bed(standing);
    check_size(on_edge, { 30, 20, 5 });
    check_on_bed(on_edge);
}

TEST_CASE("A part rests on its largest hull face even when parallel inner faces add up to more area", "[LayOnFace]")
{
    Model        model;
    ModelObject &plate = add_ribbed_plate(model);

    double area_facing_minus_x = 0.;
    for (const ModelVolume *volume : plate.volumes) {
        const indexed_triangle_set &its = volume->mesh().its;
        for (const Vec3i32 &face : its.indices) {
            const Vec3d cross = (its.vertices[face[1]] - its.vertices[face[0]]).cast<double>().cross(
                (its.vertices[face[2]] - its.vertices[face[0]]).cast<double>());
            if (cross.normalized().x() < -0.999)
                area_facing_minus_x += 0.5 * cross.norm();
        }
    }
    REQUIRE(area_facing_minus_x > 30. * 30.);

    plate.instances.front()->set_rotation({ 0, PI / 2., 0 });
    check_size(plate, { 22, 30, 30 });

    const std::vector<LayOnFacePlane> planes = instance_planes(plate);
    const int                         idx    = find_largest_plane(planes);
    REQUIRE(idx >= 0);
    CHECK_THAT(planes[idx].area, WithinAbs(30. * 30., 1e-2));
    CHECK_THAT(planes[idx].normal.z(), WithinAbs(-1., 1e-6));

    lay_on_face(plate, 0, planes[idx].normal);
    check_size(plate, { 30, 30, 22 });
    check_on_bed(plate);
}

TEST_CASE("Faces are selected in object coordinates whatever the instance rotation", "[LayOnFace]")
{
    Model        model;
    ModelObject &plate = add_ribbed_plate(model);
    plate.instances.front()->set_rotation({ 0, 0, PI / 2. });
    const Transform3d                 instance_matrix = plate.instances.front()->get_matrix_no_offset();
    const std::vector<LayOnFacePlane> planes          = lay_on_face_planes(plate, instance_matrix);
    REQUIRE_FALSE(planes.empty());

    for (size_t i = 0; i < planes.size(); ++i)
        CHECK(find_plane_at_point(planes, instance_matrix, planes[i].center, 0.01) == int(i));

    const int bottom = find_plane_at_point(planes, instance_matrix, { 15, 15, 0 }, 0.01);
    REQUIRE(bottom >= 0);
    CHECK_THAT(planes[bottom].normal.z(), WithinAbs(-1., 1e-6));
    CHECK(find_plane_by_normal(planes, { 0, 0, -1 }) == bottom);
    CHECK(find_plane_at_point(planes, instance_matrix, { 15, 15, 0.5 }, 0.01) == -1);
    CHECK(find_plane_at_point(planes, instance_matrix, { 14.5, 15, 12 }, 0.01) == -1);
}

TEST_CASE("A part too small to rest on offers no faces", "[LayOnFace]")
{
    Model model;
    CHECK(instance_planes(add_box_object(model, { 2, 2, 2 })).empty());
}
