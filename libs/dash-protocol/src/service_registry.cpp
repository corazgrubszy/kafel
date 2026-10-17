#include "services/service_registry.hpp"
#include "text/utf8.hpp"
#include "text/text_rows.hpp"
#include <charconv>

namespace kafel {
namespace {
bool text_ok(std::string_view text,std::size_t limit,bool empty=false) noexcept {
    if((text.empty()&&!empty) || text.size()>limit) return false;
    for(unsigned char c:text) if(c<32 || c==127) return false;
    while(!text.empty()) if(!utf8::decode(text).valid) return false;
    return true;
}
bool id_ok(std::string_view id) noexcept {
    if(id.empty()||id.size()>32) return false;
    for(char c:id) if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-')) return false;
    return true;
}
template<std::size_t N> bool fields(std::string_view line,std::array<std::string_view,N>& out) noexcept {
    for(std::size_t i=0;i<N;++i) {
        const auto tab=line.find('\t'); out[i]=line.substr(0,tab);
        if(tab==line.npos) return i==N-1;
        if(i==N-1) return false;
        line.remove_prefix(tab+1);
    }
    return false;
}
std::size_t tile_fields(std::string_view line,std::array<std::string_view,12>& out) noexcept {
    for(std::size_t i=0;i<out.size();++i) {
        const auto tab=line.find('\t');out[i]=line.substr(0,tab);
        if(tab==line.npos) return i+1;
        line.remove_prefix(tab+1);
    }
    return 0;
}
bool same_metric(const std::optional<MetricMessage>& a,const std::optional<MetricMessage>& b) noexcept {
    if(a.has_value()!=b.has_value()) return false;
    if(!a) return true;
    if(a->value.view()!=b->value.view()||a->unit.view()!=b->unit.view()||a->caption_count!=b->caption_count||a->level!=b->level) return false;
    for(std::size_t i=0;i<a->caption_count;++i) if(a->captions[i].view()!=b->captions[i].view()) return false;
    return true;
}
std::optional<ServiceTileMessage::State> state_of(std::string_view state) noexcept {
    if(state=="ok") return ServiceTileMessage::State::Ok;
    if(state=="warning") return ServiceTileMessage::State::Warning;
    if(state=="error") return ServiceTileMessage::State::Error;
    return {};
}
std::string_view state_name(ServiceTileMessage::State state,bool offline) noexcept {
    return offline?"OFFLINE":state==ServiceTileMessage::State::Ok?"OK":state==ServiceTileMessage::State::Warning?"UWAGA":"AWARIA";
}
bool same(const ServiceMessage& a,const ServiceMessage& b) noexcept {
    if(a.name.view()!=b.name.view()||a.tile_count!=b.tile_count) return false;
    for(std::size_t i=0;i<a.tile_count;++i) {
        const auto& x=a.tiles[i];const auto& y=b.tiles[i];
        if(x.id.view()!=y.id.view()||x.title.view()!=y.title.view()||x.detail.view()!=y.detail.view()||
           x.span!=y.span||x.state!=y.state||x.attention!=y.attention||!same_metric(x.metric,y.metric)) return false;
    }
    return true;
}
}
std::optional<ServiceMessage> parse_service_message(std::string_view wire) noexcept {
    if(wire.size()>service_packet_bytes||wire.empty()||wire.back()!='\n'||!wire.starts_with("DASH/4\t")) return {};
    ServiceMessage message;
    const auto end=wire.find('\n');
    std::array<std::string_view,3> header{};
    if(!fields(wire.substr(0,end),header)||!id_ok(header[1])||!text_ok(header[2],48)) return {};
    message.id.assign(header[1]);message.name.assign(header[2]);wire.remove_prefix(end+1);
    while(!wire.empty()) {
        if(message.tile_count==service_tile_capacity) return {};
        const auto next=wire.find('\n'); if(next==wire.npos) return {};
        std::array<std::string_view,12> row{};
        const auto count=tile_fields(wire.substr(0,next),row);
        if(count<8||row[0]!="TILE"||!id_ok(row[1])||
           (row[2]!="1"&&row[2]!="2"&&row[2]!="3")||!text_ok(row[5],48)) return {};
        const auto state=state_of(row[3]);
        if(!state||(row[4]!="normal"&&row[4]!="attention")) return {};
        for(std::size_t i=0;i<message.tile_count;++i) if(message.tiles[i].id.view()==row[1]) return {};
        auto& tile=message.tiles[message.tile_count++];
        tile.id.assign(row[1]);tile.title.assign(row[5]);
        tile.span=static_cast<unsigned>(row[2][0]-'0');tile.state=*state;tile.attention=row[4]=="attention";
        if(row[6]=="text") {
            if(count!=8||!text_ok(row[7],service_detail_bytes,true)||!TextRows::valid(row[7])) return {};
            tile.detail.assign(row[7]);
        } else if(row[6]=="metric"||row[6]=="metric-meter") {
            const bool meter=row[6]=="metric-meter";
            if(count<(meter?10U:9U)||!valid_metric_number(row[7])||!text_ok(row[8],8,true)) return {};
            tile.metric.emplace();auto& metric=*tile.metric;
            metric.value.assign(row[7]);metric.unit.assign(row[8]);metric.caption_count=count-9-(meter?1:0);
            if(meter) {
                const auto field=row[count-1]; unsigned level=0;
                if(field.empty()||field.size()>5) return {};
                const auto parsed=std::from_chars(field.data(),field.data()+field.size(),level);
                if(parsed.ec!=std::errc{}||parsed.ptr!=field.data()+field.size()||level>10000) return {};
                metric.level=level;
            }
            for(std::size_t i=0;i<metric.caption_count;++i) {
                if(!text_ok(row[9+i],64)) return {};
                metric.captions[i].assign(row[9+i]);
            }
        } else return {};
        wire.remove_prefix(next+1);
    }
    return message;
}
std::optional<ServiceRegistry::Update> ServiceRegistry::receive(const ServiceMessage& message,std::uint64_t now_ms) noexcept {
    std::size_t index=0;
    while(index<count_ && entries_[index].message.id.view()!=message.id.view()) ++index;
    const bool added=index==count_;
    if(added && count_==capacity) return {};
    auto& entry=entries_[index];
    if(message.tile_count>service_tile_capacity) return {};
    for(std::size_t i=0;i<message.tile_count;++i)
        if(message.tiles[i].metric&&(message.tiles[i].metric->caption_count>metric_caption_capacity||
           message.tiles[i].metric->auxiliary_count()>metric_caption_capacity||
           (message.tiles[i].metric->level&&*message.tiles[i].metric->level>10000))) return {};
    auto total=message.tile_count;
    for(std::size_t i=0;i<count_;++i) if(i!=index) total+=entries_[i].message.tile_count;
    if(total>service_tile_capacity) return {};
    const bool changed=added || entry.offline || !same(entry.message,message);
    entry.message=message; entry.seen_ms=now_ms; entry.offline=false;
    if(added) ++count_;
    return Update{index,added,changed};
}
bool ServiceRegistry::expire(std::size_t index,std::uint64_t now_ms) noexcept {
    if(index>=count_) return false;
    auto& entry=entries_[index];
    if(entry.offline || now_ms<entry.seen_ms || now_ms-entry.seen_ms<ttl_ms) return false;
    entry.offline=true; return true;
}
ServiceTiles ServiceRegistry::tiles() const noexcept {
    ServiceTiles result;
    for(std::size_t i=0;i<count_;++i) {
        const auto& entry=entries_[i]; const auto& message=entry.message;
        for(std::size_t j=0;j<message.tile_count;++j) {
            const auto& tile=message.tiles[j];
            result.items[result.count++]={message.id.view(),tile.id.view(),tile.span,
                {tile.title.view(),entry.offline?std::string_view("Brak komunikacji z usługą"):tile.detail.view(),
                 state_name(tile.state,entry.offline),entry.offline||tile.state!=ServiceTileMessage::State::Ok,tile.attention},
                !entry.offline&&tile.metric?std::optional<MetricContent>(tile.metric->view()):std::nullopt};
        }
    }
    return result;
}
}
