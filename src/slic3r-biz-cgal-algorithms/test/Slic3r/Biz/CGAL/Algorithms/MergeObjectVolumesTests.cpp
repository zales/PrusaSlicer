#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/CGAL/Algorithms/MergeObjectVolumes.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <optional>

using Catch::Approx;
using Slic3r::Biz::CGAL::Algorithms::merge_object_volumes;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelVolumeType;
using Slic3r::Domain::TriangleMesh;
using Slic3r::Domain::Vec3d;

namespace {

namespace TM = Slic3r::Biz::Algorithms::TriangleMesh;

// A box of the given size with its minimum corner at (x, y, z).
void add_box(ModelObject& object, ModelVolumeType type, Vec3d at, Vec3d size)
{
    auto* volume = Slic3r::Biz::Algorithms::ModelObject::
        add_volume(&object, TriangleMesh{TM::its_make_cube(size.x(), size.y(), size.z())}, type);
    volume->set_offset(at);
}

} // namespace

TEST_CASE("Merge a part with holes that do not touch", "[CGAL]")
{
    // The holes are disjoint, so they are cut in one go. Boxes keep the
    // expected volume exact, unlike the faceted primitives.
    Slic3r::Domain::Model model;
    ModelObject& object = *model.add_object();
    add_box(object, ModelVolumeType::MODEL_PART, {0., 0., 0.}, {40., 30., 10.});
    for (const double x : {5., 15., 25., 33.}) {
        add_box(object, ModelVolumeType::NEGATIVE_VOLUME, {x, 13., -5.}, {4., 4., 20.});
    }
    Slic3r::Biz::Algorithms::ModelObject::sort_volumes(&object);

    const std::optional<TriangleMesh> merged = merge_object_volumes(object);

    REQUIRE(merged.has_value());
    CHECK(TM::its_num_open_edges(merged->its) == 0);
    CHECK(Slic3r::Domain::its_volume(merged->its) == Approx(40. * 30. * 10. - 4 * 4. * 4. * 10.));
}

TEST_CASE("Merge a part with holes that overlap each other", "[CGAL]")
{
    // A counterbore sits on top of its through hole, so the two overlap and
    // cannot be cut in the same go; the result has to come out the same.
    Slic3r::Domain::Model model;
    ModelObject& object = *model.add_object();
    add_box(object, ModelVolumeType::MODEL_PART, {0., 0., 0.}, {40., 30., 10.});
    add_box(object, ModelVolumeType::NEGATIVE_VOLUME, {18., 13., -5.}, {4., 4., 20.});
    add_box(object, ModelVolumeType::NEGATIVE_VOLUME, {16., 11., 7.}, {8., 8., 5.});
    Slic3r::Biz::Algorithms::ModelObject::sort_volumes(&object);

    const std::optional<TriangleMesh> merged = merge_object_volumes(object);

    REQUIRE(merged.has_value());
    CHECK(TM::its_num_open_edges(merged->its) == 0);
    // the through hole, plus the part of the counterbore it did not take
    const double removed = 4. * 4. * 10. + (8. * 8. - 4. * 4.) * 3.;
    CHECK(Slic3r::Domain::its_volume(merged->its) == Approx(40. * 30. * 10. - removed));
}

TEST_CASE("Merge parts that do not touch each other", "[CGAL]")
{
    // Two islands and one hole in each: the union has to keep both.
    Slic3r::Domain::Model model;
    ModelObject& object = *model.add_object();
    add_box(object, ModelVolumeType::MODEL_PART, {0., 0., 0.}, {10., 10., 10.});
    add_box(object, ModelVolumeType::MODEL_PART, {30., 0., 0.}, {10., 10., 10.});
    add_box(object, ModelVolumeType::NEGATIVE_VOLUME, {3., 3., -5.}, {4., 4., 20.});
    add_box(object, ModelVolumeType::NEGATIVE_VOLUME, {33., 3., -5.}, {4., 4., 20.});
    Slic3r::Biz::Algorithms::ModelObject::sort_volumes(&object);

    const std::optional<TriangleMesh> merged = merge_object_volumes(object);

    REQUIRE(merged.has_value());
    CHECK(TM::its_num_open_edges(merged->its) == 0);
    CHECK(
        Slic3r::Domain::its_volume(merged->its) == Approx(2. * (10. * 10. * 10. - 4. * 4. * 10.))
    );
}
