#pragma once

#include <cmath>
#include <cstdint>
#include <optional>

namespace seethis::core {

using DisplayId = std::uint64_t;

enum class CoordinateUnit {
  kLogicalPoints,
  kBackingPixels,
};

struct Point2D {
  double x = 0.0;
  double y = 0.0;
};

struct Size2D {
  double width = 0.0;
  double height = 0.0;
};

struct DisplayPoint {
  DisplayId display_id = 0;
  CoordinateUnit unit = CoordinateUnit::kLogicalPoints;
  Point2D position;
  double backing_scale = 1.0;

  [[nodiscard]] bool IsValid() const {
    return display_id != 0 && std::isfinite(position.x) &&
           std::isfinite(position.y) && std::isfinite(backing_scale) &&
           backing_scale > 0.0;
  }
};

struct DisplayGeometry {
  DisplayId display_id = 0;
  Point2D global_origin_logical;
  Size2D logical_size;
  double backing_scale = 1.0;

  [[nodiscard]] bool IsValid() const {
    return display_id != 0 && std::isfinite(global_origin_logical.x) &&
           std::isfinite(global_origin_logical.y) &&
           std::isfinite(logical_size.width) &&
           std::isfinite(logical_size.height) && logical_size.width > 0.0 &&
           logical_size.height > 0.0 && std::isfinite(backing_scale) &&
           backing_scale > 0.0;
  }

  [[nodiscard]] bool ContainsGlobalLogical(Point2D point) const {
    return IsValid() && point.x >= global_origin_logical.x &&
           point.y >= global_origin_logical.y &&
           point.x < global_origin_logical.x + logical_size.width &&
           point.y < global_origin_logical.y + logical_size.height;
  }

  // Native pointer samples on an edge belong to the frozen display. Samples
  // outside this inclusive local box are retained only as a clipped boundary
  // and create a discontinuity until re-entry.
  [[nodiscard]] bool ContainsLocalLogical(Point2D point) const {
    return IsValid() && point.x >= 0.0 && point.y >= 0.0 &&
           point.x <= logical_size.width && point.y <= logical_size.height;
  }

  [[nodiscard]] std::optional<DisplayPoint> FromGlobalLogical(
      Point2D point) const {
    if (!ContainsGlobalLogical(point)) {
      return std::nullopt;
    }
    return DisplayPoint{
        display_id,
        CoordinateUnit::kLogicalPoints,
        {point.x - global_origin_logical.x,
         point.y - global_origin_logical.y},
        backing_scale,
    };
  }

  [[nodiscard]] std::optional<Point2D> ToGlobalLogical(
      const DisplayPoint& point) const {
    if (!point.IsValid() || point.display_id != display_id ||
        point.unit != CoordinateUnit::kLogicalPoints ||
        point.backing_scale != backing_scale) {
      return std::nullopt;
    }
    return Point2D{global_origin_logical.x + point.position.x,
                   global_origin_logical.y + point.position.y};
  }
};

[[nodiscard]] inline std::optional<DisplayPoint> ConvertCoordinateUnit(
    const DisplayPoint& point, CoordinateUnit target_unit) {
  if (!point.IsValid()) {
    return std::nullopt;
  }
  if (point.unit == target_unit) {
    return point;
  }

  DisplayPoint converted = point;
  converted.unit = target_unit;
  if (target_unit == CoordinateUnit::kBackingPixels) {
    converted.position.x *= point.backing_scale;
    converted.position.y *= point.backing_scale;
  } else {
    converted.position.x /= point.backing_scale;
    converted.position.y /= point.backing_scale;
  }
  return converted;
}

}  // namespace seethis::core
