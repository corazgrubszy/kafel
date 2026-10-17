#include "services/service_registry.hpp"
#include "dashboard/dashboard_view.hpp"
#include <cstdio>
#include <string>
void check(bool,const char*);
void test_services() {
    using namespace kafel;
    const auto message=parse_service_message("DASH/4\tinfohub\tInfoHub\nTILE\tstatus\t3\tok\tnormal\tInfoHub\ttext\tPogoda i śmieci OK\n");
    check(message.has_value(),"valid Polish service snapshot");
    ServiceRegistry registry;
    auto change=registry.receive(*message,100);
    check(change&&change->added&&change->changed&&registry.size()==1,"first heartbeat registers");
    check(!registry.receive(*message,200)->changed,"identical heartbeat only updates liveness");
    check(!registry.expire(0,15199)&&registry.expire(0,15200),"TTL uses last receive and expires at boundary");
    check(registry.tiles().items[0].content.alarm&&registry.tiles().items[0].content.status=="OFFLINE","silent service becomes alarm");
    check(!registry.expire(0,16000),"offline does not repeatedly invalidate");
    check(registry.receive(*message,17000)->changed&&!registry.tiles().items[0].content.alarm,"service recovery clears alarm");
    auto warning=*message;warning.tiles[0].state=ServiceTileMessage::State::Warning;
    registry.receive(warning,18000);
    check(registry.tiles().items[0].content.status=="UWAGA"&&registry.tiles().items[0].content.alarm,"warning belongs to the tile");
    for(std::size_t i=1;i<ServiceRegistry::capacity;++i) {
        auto next=*message;char id[16];std::snprintf(id,sizeof(id),"service-%zu",i);next.id.assign(id);
        check(registry.receive(next,0)->added,"register up to fixed service capacity");
    }
    auto extra=*message;extra.id.assign("extra");check(!registry.receive(extra,0),"capacity bounded");
    std::puts("PASS: service heartbeat, capacity, TTL and recovery");
}
