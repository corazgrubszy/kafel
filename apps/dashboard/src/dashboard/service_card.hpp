#pragma once
#include "dash5/state.hpp"
#include "ui/card.hpp"
#include <string>
#include <utility>
namespace kafel {
// Checked value. Fields are constructor input; callers cannot mutate an accepted
// card into an online/healthy state without its prepared frame.
// Moving transfers the checked value to the destination. The source may only
// be destroyed or assigned a new card; do not inspect, copy or publish it.
// This also applies to a source passed to move assignment.
class ServiceCard {
  public:
    struct Fields {
        std::string owner, id, title;
        dash5::TileState state = dash5::TileState::Ok;
        std::string message;
        bool attention = false, offline = false;
        std::shared_ptr<const ui::CardFrame> frame;
        bool operator==(const Fields &) const = default;
    };
    ServiceCard(Fields);
    ServiceCard(const ServiceCard &) = default;
    ServiceCard(ServiceCard &&) noexcept = default;
    ServiceCard &operator=(ServiceCard other) noexcept {
        using std::swap;
        swap(fields_, other.fields_);
        return *this;
    }
    const Fields &data() const noexcept { return fields_; }
    const std::string &owner() const noexcept { return fields_.owner; }
    const std::string &id() const noexcept { return fields_.id; }
    const std::string &title() const noexcept { return fields_.title; }
    const std::string &message() const noexcept { return fields_.message; }
    dash5::TileState state() const noexcept { return fields_.state; }
    bool attention() const noexcept { return fields_.attention; }
    bool offline() const noexcept { return fields_.offline; }
    const std::shared_ptr<const ui::CardFrame> &frame() const noexcept { return fields_.frame; }
    bool operator==(const ServiceCard &) const = default;
    bool failed() const noexcept { return offline() || state() == dash5::TileState::Error; }
    bool has_content() const noexcept { return !failed() && bool(frame()); }

  private:
    Fields fields_;
};
} // namespace kafel
