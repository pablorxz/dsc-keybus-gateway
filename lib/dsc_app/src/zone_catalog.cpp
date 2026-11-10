// SPDX-License-Identifier: GPL-3.0-or-later
#include "zone_catalog.h"
namespace dsc::app {
bool ZoneCatalog::add(std::uint8_t number,const char *name,const char *group,const char *category) {
  if (number<1 || number>64 || !name || !group || !category || !*name || !*group) return false;
  auto &entry=zones_[number-1];
  if (entry.number) return false;
  entry={number,name,group,category};
  return true;
}
}  // namespace dsc::app
