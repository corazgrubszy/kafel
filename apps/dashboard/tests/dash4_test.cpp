#include "services/service_registry.hpp"
#include "dashboard/dashboard_view.hpp"
#include <fstream>
#include <iterator>
#include <string>
#include <cstdio>
void check(bool,const char*);
void test_dash4() {
    using namespace kafel;
    std::ifstream file("apps/dashboard/tests/fixtures/infohub_dash4.txt");
    const std::string wire((std::istreambuf_iterator<char>(file)),{});
    check(!wire.empty(),"InfoHub golden datagram is available");
    const auto parsed=parse_service_message(wire);
    check(parsed&&parsed->tile_count==2,"InfoHub DASH/4 snapshot parses");
    check(parsed->tiles[0].id.view()=="weather"&&parsed->tiles[0].span==2&&
          parsed->tiles[1].id.view()=="waste"&&parsed->tiles[1].span==2&&parsed->tiles[1].attention,
          "golden snapshot preserves IDs, widths and reminder");
    check(parsed->tiles[0].metric&&parsed->tiles[0].metric->value.view()=="8"&&
          parsed->tiles[0].metric->captions[1].view()=="Wiatr: 3.3 m/s","InfoHub golden metric payload agrees with producer");
    check(!parse_service_message("DASH/2\tx\tX\tok\tPlain\n")&&
          !parse_service_message("DASH/2\tx\tX\tok\tattention\tPickup\n"),
          "unsupported protocol versions are rejected");
    const auto empty=parse_service_message("DASH/4\tinfohub\tInfoHub\n");
    check(empty&&empty->tile_count==0,"empty complete snapshot is legal");
    for(auto bad:{"DASH/4\ta\tA\n\n","DASH/4\ta\tA\nTILE\tx\t0\tok\tnormal\tA\ttext\tx\n",
        "DASH/4\ta\tA\nTILE\tx\t4\tok\tnormal\tA\ttext\tx\n",
        "DASH/4\ta\tA\nTILE\tx\t2\tbad\tnormal\tA\ttext\tx\n",
        "DASH/4\ta\tA\nTILE\tx\t2\tok\tbad\tA\ttext\tx\n",
        "DASH/4\ta\tA\nTILE\tx\t2\tok\tnormal\tA\ttext\tx\nTILE\tx\t1\tok\tnormal\tB\ttext\ty\n",
        "DASH/4\ta\tA\nTILE\tx\t2\tok\tnormal\tA\ttext\t\xc0\xaf\n",
        "DASH/4\ta\tA\nTILE\tx\t2\tok\tnormal\tA\ttext\tx\textra\n",
        "DASH/4\t../a\tA\n","DASH/4\ta\tA","DASH/4\ta\tA\nTILE\tx\t2\tok\tnormal\tA\ttext\tx"})
        check(!parse_service_message(bad),"malformed complete snapshot rejected atomically");
    check(parse_service_message("DASH/4\ta\tA\nTILE\tx\t1\tok\tnormal\tA\ttext\t\n").has_value(),"empty tile detail is legal");
    std::string full="DASH/4\tfull\tFull\n";
    for(unsigned i=0;i<service_tile_capacity;++i)
        full+="TILE\tt"+std::to_string(i)+"\t1\tok\tnormal\tTitle\ttext\t"+std::string(service_detail_bytes,'x')+"\n";
    check(parse_service_message(full).has_value(),"all bounded tile details fit datagram budget");
    check(!parse_service_message(full+"TILE\textra\t1\tok\tnormal\tTitle\ttext\tx\n"),"too many tile records rejected");
    check(!parse_service_message("DASH/4\ta\tA\nTILE\tx\t1\tok\tnormal\tA\ttext\t"+std::string(513,'x')+"\n"),"per-tile byte limit retained");
    ServiceRegistry registry;
    check(registry.receive(*parsed,100)->added,"one service registers two tiles");
    auto tiles=registry.tiles();
    check(registry.size()==1&&tiles.count==2&&tiles.items[0].service_id=="infohub", "liveness remains service-scoped");
    check(!registry.receive(*parsed,200)->changed,"identical snapshot only refreshes liveness");
    auto source_error=*parsed;source_error.tiles[0].state=ServiceTileMessage::State::Error;
    registry.receive(source_error,300);tiles=registry.tiles();
    check(tiles.items[0].content.status=="AWARIA"&&tiles.items[1].content.status=="OK"&&tiles.items[1].content.attention,
          "source error never contaminates sibling state or reminder");
    check(!registry.expire(0,15299)&&registry.expire(0,15300),"one TTL covers complete snapshot");
    tiles=registry.tiles();check(tiles.items[0].content.status=="OFFLINE"&&tiles.items[1].content.status=="OFFLINE", "lost publisher makes all tiles offline");
    registry.receive(source_error,16000);tiles=registry.tiles();
    check(tiles.items[0].content.status=="AWARIA"&&tiles.items[1].content.status=="OK","recovery restores individual states");
    const auto sibling=parse_service_message("DASH/4\tother\tOther\nTILE\tweather\t1\tok\tnormal\tOther weather\ttext\tWorks\n");
    check(registry.receive(*sibling,16000)->added,"different services may use the same tile ID");
    tiles=registry.tiles();check(tiles.count==3&&tiles.items[2].service_id=="other"&&tiles.items[2].tile_id=="weather",
          "composite identity separates tiles from different services");
    // The new snapshot cannot evict existing tiles by overfilling the registry.
    const auto crowded=parse_service_message(full);check(!registry.receive(*crowded,17000),"global tile budget checked before commit");
    check(registry.size()==2&&registry.tiles().count==3,"rejected snapshot leaves all services intact");
    auto overfull=*crowded;overfull.id.assign("infohub");
    check(!registry.receive(overfull,30000)&&registry.tiles().count==3,"overfull replacement cannot partially remove sibling tiles");
    check(!registry.expire(0,30999)&&registry.expire(0,31000),"rejected replacement never refreshes liveness");
    auto removed=*parsed;removed.tile_count=1;registry.receive(removed,32000);
    check(registry.tiles().count==2,"omitted tile is removed");
    registry.receive(*empty,33000);check(registry.size()==2&&registry.tiles().count==1,"zero-tile service keeps heartbeat and removes its tiles");
    ServiceRegistry slots;
    for(unsigned i=0;i<ServiceRegistry::capacity;++i) {
        auto service=*empty;service.id.assign("s"+std::to_string(i));
        check(slots.receive(service,0).has_value(),"zero-tile services count toward service capacity");
    }
    check(!slots.receive(*empty,0),"service capacity is independent of tile capacity");

    ServiceRegistry live;live.receive(*parsed,0);tiles=live.tiles();
    DashboardView view;view.update_clock({16,35},0);
    check(view.replace_services(tiles.view(),0)&&view.service_count()==2,"view accepts atomic tile snapshot");
    check(view.ribbon().layout().tile(3).width==153&&view.ribbon().layout().tile(4).width==153,"declared widths reach layout");
    std::array<Pixel,240*320> pixels{};Surface surface(pixels,240,320,240);view.render(surface,0);
    auto swapped_attention=tiles;
    swapped_attention.items[0].content.alarm=true;swapped_attention.items[0].content.status="AWARIA";
    swapped_attention.items[1].content.attention=false;
    check(view.replace_services(swapped_attention.view(),1)&&!view.needs_redraw(1),
          "atomic offscreen attention transfer does not redraw unchanged main star");
    view.replace_services(tiles.view(),2);
    using K=TouchSample::Kind;
    view.pointer({K::Down,210,270},100);view.pointer({K::Move,10,270},200);view.pointer({K::Up,10,270},210);
    view.render(surface,900);
    check(view.selected_tile()==4,"gesture reaches waste tile");
    auto reordered=tiles;std::swap(reordered.items[0],reordered.items[1]);
    check(view.replace_services(reordered.view(),901)&&view.selected_tile()==3,"selected tile survives reorder by composite identity");
    view.render(surface,901);const auto before=pixels;
    auto invalid=reordered;invalid.items[1].tile_id=invalid.items[0].tile_id;
    check(!view.replace_services(invalid.view(),902),"duplicate tile IDs rejected before view mutation");
    view.render(surface,902);check(pixels==before,"rejected view snapshot leaves pixels intact");
    check(view.replace_services(reordered.view(),903)&&!view.needs_redraw(903),"identical snapshot does not write framebuffer");
    check(view.replace_services({},904)&&view.service_count()==0&&view.ribbon().layout().size()==3,"empty set leaves system metrics available");
    const std::array<ServiceTileStatus,1> narrow={{{"narrow","one",1,{"Title","Long narrow detail","OFFLINE",true,false}}}};
    check(view.replace_services(narrow,905)&&view.ribbon().layout().tile(3).width==74,"one-column service uses real narrow geometry");
    view.render(surface,905);
    std::puts("PASS: DASH/4 atomic snapshots, bounds, independent states, TTL, identity and spans");
}
