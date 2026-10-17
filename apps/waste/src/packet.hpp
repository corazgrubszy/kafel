#pragma once
#include "schedule.hpp"
namespace kafel::waste {
std::string dashboard_packet(const Schedule* schedule,Date today,std::int64_t unix_seconds,
                             bool reload_failed=false,unsigned span=1);
}
