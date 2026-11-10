// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>

namespace dsc::app {
struct ZoneMetadata {
  std::uint8_t number{0};
  const char *name{nullptr};
  const char *group{nullptr};
  const char *category{nullptr};
};
class ZoneCatalog {
 public:
  bool add(std::uint8_t number, const char *name, const char *group, const char *category);
  const std::array<ZoneMetadata,64> &zones() const { return zones_; }
 private:
  std::array<ZoneMetadata,64> zones_{};
};
}  // namespace dsc::app
