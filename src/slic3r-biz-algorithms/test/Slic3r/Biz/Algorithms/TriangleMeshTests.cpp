#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using Catch::Approx;
using Slic3r::Biz::Algorithms::Scaling::scaled;
using Slic3r::Biz::Algorithms::TriangleMesh::its_make_cone;
using Slic3r::Biz::Algorithms::TriangleMesh::its_make_extrusion;
using Slic3r::Biz::Algorithms::TriangleMesh::its_num_open_edges;
using Slic3r::Domain::ExPolygon;
using Slic3r::Domain::ExPolygons;
using Slic3r::Domain::its_volume;
using Slic3r::Domain::Polygon;

namespace {

Polygon rectangle(double x0, double y0, double x1, double y1)
{
    Polygon poly;
    poly.points.emplace_back(scaled(x0), scaled(y0));
    poly.points.emplace_back(scaled(x1), scaled(y0));
    poly.points.emplace_back(scaled(x1), scaled(y1));
    poly.points.emplace_back(scaled(x0), scaled(y1));
    return poly;
}

// A regular polygon around (cx, cy), wound counter clockwise.
Polygon regular_polygon(size_t sides, double radius, double cx, double cy)
{
    Polygon poly;
    for (size_t i = 0; i < sides; ++i) {
        const double angle = 2. * std::numbers::pi * double(i) / double(sides);
        const double x     = cx + radius * std::cos(angle);
        const double y     = cy + radius * std::sin(angle);
        poly.points.emplace_back(scaled(x), scaled(y));
    }
    return poly;
}

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

TEST_CASE("Extrude a rectangle", "[algorithms][algorithms-trianglemesh]")
{
    const ExPolygons shape{ExPolygon{rectangle(0., 0., 40., 30.)}};

    const indexed_triangle_set its = its_make_extrusion(shape, 5.);

    CHECK(its_num_open_edges(its) == 0);
    CHECK(its_volume(its) == Approx(40. * 30. * 5.).epsilon(1e-4));
}

TEST_CASE("Extrude a shape with a hole", "[algorithms][algorithms-trianglemesh]")
{
    // A hole is wound the other way round, the way ExPolygon expects it.
    Polygon hole = rectangle(10., 10., 30., 20.);
    std::ranges::reverse(hole.points);
    const ExPolygons shape{ExPolygon{rectangle(0., 0., 40., 30.), hole}};

    const indexed_triangle_set its = its_make_extrusion(shape, 5.);

    CHECK(its_num_open_edges(its) == 0);
    CHECK(its_volume(its) == Approx((40. * 30. - 20. * 10.) * 5.).epsilon(1e-4));
}

TEST_CASE("Extrude two separate shapes", "[algorithms][algorithms-trianglemesh]")
{
    const ExPolygon left{rectangle(0., 0., 10., 10.)};
    const ExPolygon right{rectangle(20., 0., 30., 10.)};
    const ExPolygons shape{left, right};

    const indexed_triangle_set its = its_make_extrusion(shape, 4.);

    CHECK(its_num_open_edges(its) == 0);
    CHECK(its_volume(its) == Approx(2. * 10. * 10. * 4.).epsilon(1e-4));
}

TEST_CASE("Extrude a sampled circle", "[algorithms][algorithms-trianglemesh]")
{
    // Coordinates off the float grid: the walls and the caps only close up
    // when both unscale through the same arithmetic.
    const size_t sides = 64;
    const double r     = 19.4;
    const ExPolygons shape{ExPolygon{regular_polygon(sides, r, 237.1, 198.6)}};

    const indexed_triangle_set its = its_make_extrusion(shape, 5.);

    CHECK(its_num_open_edges(its) == 0);
    // area of a regular polygon inscribed in the circle
    const double area =
        0.5 * double(sides) * r * r * std::sin(2. * std::numbers::pi / double(sides));
    CHECK(its_volume(its) == Approx(area * 5.).epsilon(1e-3));
}

TEST_CASE("Extrude a degenerate footprint", "[algorithms][algorithms-trianglemesh]")
{
    const ExPolygons shape{ExPolygon{rectangle(0., 0., 40., 30.)}};

    CHECK(its_make_extrusion(ExPolygons{}, 5.).indices.empty());
    CHECK(its_make_extrusion(shape, 0.).indices.empty());
    CHECK(its_make_extrusion(shape, -5.).indices.empty());
}

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
