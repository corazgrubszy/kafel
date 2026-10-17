#include "packet.hpp"
#include <array>
#include <stdexcept>
namespace kafel::waste {
std::string dashboard_packet(const Schedule* schedule,Date today,std::int64_t seconds,bool failed,unsigned span) {
    if(span<1||span>2)throw std::invalid_argument("Waste span must be 1 or 2");
    const std::string prefix="DASH/4\tkafel-waste\tKafel Waste\nTILE\twaste\t"+std::to_string(span)+"\t";
    if(!schedule)return prefix+"error\tnormal\tŚmieci\ttext\tBrak harmonogramu\n";
    const auto selected=select(*schedule,today,seconds);
    if(!selected)return prefix+"warning\tnormal\tŚmieci\ttext\tBrak terminów\n";
    constexpr std::array colors={"B8C0C8","008F39","0072CE","E6B800","A0522D","008F39"};
    std::string detail;
    for(std::size_t i=0;i<colors.size();++i)if(selected->day.fractions&(1U<<i))
        detail+=std::string("[#")+colors[i]+"]🗑[/]";
    detail+="\u2028";
    if(failed)detail+="Nieaktualne: ";
    if(selected->when==Selection::When::Today)detail+="Dzisiaj";
    else if(selected->when==Selection::When::Tomorrow)detail+="Jutro";
    else {
        constexpr std::array months={"stycznia","lutego","marca","kwietnia","maja","czerwca","lipca","sierpnia","września","października","listopada","grudnia"};
        const auto date=std::chrono::year_month_day(selected->day.date);
        detail+=std::to_string(static_cast<unsigned>(date.day()))+" "+months[static_cast<unsigned>(date.month())-1];
    }
    return prefix+(failed?"warning":"ok")+"\t"+(selected->attention?"attention":"normal")+"\tŚmieci\ttext\t"+detail+"\n";
}
}
