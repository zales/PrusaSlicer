#pragma once

#include <optional>

#include "Slic3r/Domain/TriangleMesh.hpp"

namespace Slic3r::Domain {
class ModelObject;
} // namespace Slic3r::Domain

namespace Slic3r::Biz::CGAL::Algorithms {

/**
 * @brief Merge the object volumes into a single mesh in object coordinates, with model
 *        parts united and negative volumes subtracted.
 *
 * @return std::nullopt when the booleans could not be performed on the input meshes.
 */
std::optional<Domain::TriangleMesh> merge_object_volumes(const Domain::ModelObject& model_object);

/**
 * @brief Replace the model parts and negative volumes of an object by their boolean result.
 *
 * The model parts are united and the negative volumes subtracted, see merge_object_volumes(), and
 * the result is put back as a single model part. Modifiers and support blockers/enforcers are left
 * as they are. Settings attached to the replaced volumes are dropped.
 *
 * @return false, leaving @p model_object untouched, when the booleans could not be performed.
 */
bool merge_object_parts(Domain::ModelObject& model_object);

} // namespace Slic3r::Biz::CGAL::Algorithms
