#pragma once
#include <memory>
#include <utility>
#include <vector>
namespace ui {
class CardFrame;
}
namespace kafel::dash5 {
struct PreparedTiles {
    explicit PreparedTiles(std::vector<std::shared_ptr<const ui::CardFrame>> value = {})
        : frames(std::move(value)) {}
    const std::vector<std::shared_ptr<const ui::CardFrame>> frames;
};
} // namespace kafel::dash5
