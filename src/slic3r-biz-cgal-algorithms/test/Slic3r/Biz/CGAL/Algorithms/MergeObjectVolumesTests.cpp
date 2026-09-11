#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/CGAL/Algorithms/MergeObjectVolumes.hpp"
#include "Slic3r/Domain/Model.hpp"

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

namespace {

Domain::ModelVolume* add_box(
    Domain::ModelObject& object,
    const Domain::Vec3d& size,
    const Domain::Vec3d& offset,
    Domain::ModelVolumeType type
)
{
    Domain::ModelVolume* volume = Biz::Algorithms::ModelObject::add_volume(
        &object,
        Biz::Algorithms::TriangleMesh::its_make_cube(size.x(), size.y(), size.z()),
        type
    );
    volume->set_offset(offset);
    return volume;
}

// A 10 mm cube with a 4 x 4 mm hole straight through it. The hole reaches past the cube in Z
// whether add_volume() centres the geometry or keeps its corner at the offset.
void add_cube_with_hole(Domain::ModelObject& object)
{
    add_box(object, {10., 10., 10.}, Domain::Vec3d::Zero(), Domain::ModelVolumeType::MODEL_PART);
    add_box(object, {4., 4., 14.}, {1., 1., -2.}, Domain::ModelVolumeType::NEGATIVE_VOLUME);
}

} // namespace

TEST_CASE("merge_object_parts subtracts negative volumes from the parts", "[CGAL]")
{
    Domain::Model model;
    Domain::ModelObject& object = *model.add_object();
    add_cube_with_hole(object);

    REQUIRE(Biz::CGAL::Algorithms::merge_object_parts(object));

    REQUIRE(object.volumes.size() == 1);
    const Domain::ModelVolume& merged = *object.volumes.front();
    CHECK(merged.is_model_part());
    CHECK_THAT(Domain::its_volume(merged.mesh().its), WithinAbs(1000. - 4. * 4. * 10., 1e-2));
}

TEST_CASE("merge_object_parts keeps modifiers", "[CGAL]")
{
    Domain::Model model;
    Domain::ModelObject& object = *model.add_object();
    add_cube_with_hole(object);
    add_box(
        object,
        {10., 10., 2.},
        Domain::Vec3d::Zero(),
        Domain::ModelVolumeType::PARAMETER_MODIFIER
    );

    REQUIRE(Biz::CGAL::Algorithms::merge_object_parts(object));

    REQUIRE(object.volumes.size() == 2);
    CHECK(object.volumes[0]->is_model_part());
    CHECK(object.volumes[1]->is_modifier());
}
