#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Biz/Algorithms/Tesselate.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using Slic3r::Biz::Algorithms::Scaling::scaled;
using Slic3r::Biz::Algorithms::Tesselate::NORMALS_DOWN;
using Slic3r::Biz::Algorithms::Tesselate::NORMALS_UP;
using Slic3r::Biz::Algorithms::Tesselate::triangulate_expolygons_3d;
using Slic3r::Biz::Algorithms::Tesselate::wall_strip;
using Slic3r::Biz::Algorithms::TriangleMesh::its_merge_vertices;
using Slic3r::Biz::Algorithms::TriangleMesh::its_num_open_edges;
using Slic3r::Biz::Algorithms::TriangleMesh::its_remove_degenerate_faces;
using Slic3r::Domain::ExPolygon;
using Slic3r::Domain::ExPolygons;
using Slic3r::Domain::its_merge;
using Slic3r::Domain::Point;
using Slic3r::Domain::Polygon;

namespace {

// A regular polygon around (cx, cy). Sampling a circle is what makes this a
// regression test: the coordinates are not exactly representable in single
// precision, so the walls and the caps only meet when both unscale the same
// way. An axis aligned box would pass either way.
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

// Extrudes the outline from z = 0 to z = height the way the slices to mesh
// conversion does: tesselated caps top and bottom, a wall strip in between.
indexed_triangle_set extrude(const Polygon& outline, double height)
{
    const ExPolygons shape{ExPolygon{outline}};

    indexed_triangle_set its;
    its_merge(its, triangulate_expolygons_3d(shape, 0., NORMALS_DOWN));
    its_merge(its, wall_strip(outline, 0., height));
    its_merge(its, triangulate_expolygons_3d(shape, height, NORMALS_UP));
    its_merge_vertices(its);
    its_remove_degenerate_faces(its);
    return its;
}

} // namespace

TEST_CASE("Wall strip welds onto tesselated caps", "[algorithms][algorithms-tesselate]")
{
    const Polygon outline = regular_polygon(64, 19.4, 24.3, 17.9);

    const indexed_triangle_set its = extrude(outline, 5.);

    // Both ends of every wall vertex are shared with a cap vertex, so the
    // extrusion has exactly one vertex per outline point per z level.
    CHECK(its.vertices.size() == 2 * outline.points.size());
    CHECK(its_num_open_edges(its) == 0);
}

TEST_CASE(
    "Wall strip welds onto tesselated caps off the float grid",
    "[algorithms][algorithms-tesselate]"
)
{
    // Away from the origin the spacing of the representable floats grows, so
    // this is where unscaling in single precision drifts the furthest.
    const Polygon outline = regular_polygon(64, 19.4, 237.1, 198.6);

    const indexed_triangle_set its = extrude(outline, 5.);

    CHECK(its.vertices.size() == 2 * outline.points.size());
    CHECK(its_num_open_edges(its) == 0);
}
