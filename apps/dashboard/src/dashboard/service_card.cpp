#include "service_card.hpp"
#include "text/utf8.hpp"
#include <stdexcept>
namespace kafel {
ServiceCard::ServiceCard(Fields fields) : fields_(std::move(fields)) {
    const auto identifier = [](const std::string &value) {
        return !value.empty() && value.size() <= 32 &&
               value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._-") == value.npos;
    };
    const auto &f = fields_;
    const bool state = f.state == dash5::TileState::Ok || f.state == dash5::TileState::Warning ||
                       f.state == dash5::TileState::Error;
    if (!identifier(f.owner) || !identifier(f.id) || f.title.empty() || f.title.size() > 48 ||
        f.message.size() > 96 || !utf8::valid(f.title, true) || !utf8::valid(f.message, true) || !state ||
        (!f.offline && f.state != dash5::TileState::Error && !f.frame))
        throw std::invalid_argument("invalid service card");
}
} // namespace kafel
