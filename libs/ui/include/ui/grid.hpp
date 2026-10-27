#pragma once
#include "geometry.hpp"
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
namespace ui {
// Integer grid: distribute rounding across columns, keep every gap exact.
// A spanning cell consumes its internal gaps. No allocation or widget state.
class Grid {
  public:
    Grid(PixelRect bounds, int columns, int gap) : bounds_(bounds), columns_(columns), gap_(gap) {
        const auto available =
            static_cast<std::int64_t>(bounds.width) - (static_cast<std::int64_t>(columns) - 1) * gap;
        if (columns < 1 || columns > 64 || gap < 0 || bounds.width < 1 || bounds.height < 1 ||
            available < columns ||
            static_cast<std::int64_t>(bounds.x) + bounds.width > std::numeric_limits<int>::max() ||
            static_cast<std::int64_t>(bounds.y) + bounds.height > std::numeric_limits<int>::max())
            throw std::invalid_argument("invalid grid geometry");
        available_ = static_cast<int>(available);
    }
    std::optional<PixelRect> cell(int column, int span = 1) const noexcept {
        if (column < 0 || span < 1 || span > columns_ || column > columns_ - span)
            return std::nullopt;
        const int x = column_start(column);
        const int end = column + span;
        const int right =
            static_cast<int>(bounds_.x + static_cast<std::int64_t>(available_) * end / columns_ +
                             static_cast<std::int64_t>(gap_) * (end - 1));
        return PixelRect{x, bounds_.y, right - x, bounds_.height};
    }

  private:
    PixelRect bounds_;
    int columns_, gap_, available_ = 0;
    int column_start(int index) const noexcept {
        return static_cast<int>(bounds_.x + static_cast<std::int64_t>(available_) * index / columns_ +
                                static_cast<std::int64_t>(gap_) * index);
    }
};
} // namespace ui
