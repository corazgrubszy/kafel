#pragma once
#include "common/fixed_text.hpp"
#include <span>
#include <optional>
namespace kafel {
inline constexpr std::size_t metric_caption_capacity=3;
struct MetricContent {
    std::string_view value,unit;
    std::array<std::string_view,metric_caption_capacity> captions{};
    std::size_t caption_count=0;
    // Optional final auxiliary page, normalized to 0..10000. No domain units.
    std::optional<unsigned> level{};
    std::size_t auxiliary_count() const noexcept { return caption_count+(level?1:0); }
};
struct MetricMessage {
    FixedText<9> value,unit;
    std::array<FixedText<65>,metric_caption_capacity> captions{};
    std::size_t caption_count=0;
    // Optional final auxiliary page, normalized to 0..10000. No domain units.
    std::optional<unsigned> level{};
    std::size_t auxiliary_count() const noexcept { return caption_count+(level?1:0); }
    MetricContent view() const noexcept {
        MetricContent result{value.view(),unit.view(),{},caption_count,level};
        for(std::size_t i=0;i<caption_count&&i<captions.size();++i) result.captions[i]=captions[i].view();
        return result;
    }
    void assign(const MetricContent& content) noexcept {
        value.assign(content.value);unit.assign(content.unit);caption_count=content.caption_count;level=content.level;
        for(std::size_t i=0;i<caption_count;++i) captions[i].assign(content.captions[i]);
    }
};
// Empty means unavailable. Otherwise ASCII -?digits(.digits)?, no exponent.
inline bool valid_metric_number(std::string_view value) noexcept {
    if(value.size()>8) return false;
    if(value.empty()) return true;
    if(value.front()=='-') value.remove_prefix(1);
    bool digits=false,point=false,after=false;
    for(char c:value) {
        if(c=='.'&&digits&&!point) { point=true;continue; }
        if(c<'0'||c>'9') return false;
        digits=true;if(point) after=true;
    }
    return digits&&(!point||after);
}
}
