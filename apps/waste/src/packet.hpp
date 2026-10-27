#pragma once
#include "dash5/bundle.hpp"
#include "schedule.hpp"
namespace kafel::waste {
const dash5::AssetBundle &dashboard_assets();
dash5::Json dashboard_document(const Schedule *, Date, bool reload_failed = false);
std::string dashboard_tiles(const Schedule *, Date, bool reload_failed = false);
} // namespace kafel::waste
