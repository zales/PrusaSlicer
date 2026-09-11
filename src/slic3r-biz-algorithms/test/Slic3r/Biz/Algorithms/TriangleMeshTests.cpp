#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using Slic3r::Biz::Algorithms::TriangleMesh::its_make_cone;
using Slic3r::Biz::Algorithms::TriangleMesh::its_num_open_edges;

namespace {

// Pairs of distinct vertices that land on top of each other. The repeated seam
// vertex is not bit for bit equal to the first one, so this has to measure a
// distance: anything far below the facet spacing is a vertex that should not
// be there, and the sliver faces around it are what the booleans trip over.
size_t count_coincident_vertices(const indexed_triangle_set& its, float tolerance)
{
    size_t pairs = 0;
    for (size_t i = 0; i < its.vertices.size(); ++i) {
        for (size_t j = i + 1; j < its.vertices.size(); ++j) {
            if ((its.vertices[i] - its.vertices[j]).norm() < tolerance) {
                ++pairs;
            }
        }
    }
    return pairs;
}

} // namespace

TEST_CASE("Cone closes its ring of vertices", "[algorithms][algorithms-trianglemesh]")
{
    // The default facet angle divides the turn evenly, which is where
    // accumulating the angle in a double overshoots and repeats the seam
    // vertex. The others do not divide evenly and have to close up as well.
    const double r = 5.;
    for (const double fa : {2. * std::numbers::pi / 360., 0.1, 0.37, 1.}) {
        const indexed_triangle_set cone = its_make_cone(r, 12., fa);

        // a hundredth of the shortest edge the facets are meant to have
        const auto tolerance = float(2. * r * std::sin(fa / 2.) / 100.);
        CHECK(count_coincident_vertices(cone, tolerance) == 0);
        CHECK(its_num_open_edges(cone) == 0);
    }
}

TEST_CASE("Cone has one vertex per facet", "[algorithms][algorithms-trianglemesh]")
{
    const double fa = 2. * std::numbers::pi / 360.;

    const indexed_triangle_set cone = its_make_cone(5., 12., fa);

    // the base centre, the apex and one vertex per facet around the base
    CHECK(cone.vertices.size() == 2 + 360);
}
