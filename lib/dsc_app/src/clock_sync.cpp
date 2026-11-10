// SPDX-License-Identifier: GPL-3.0-or-later
#include "clock_sync.h"
#include <cstdio>
namespace dsc::app {
bool valid_clock(const PanelClock &c) {
  if (c.year<2000 || c.year>2069 || c.month<1 || c.month>12 || c.hour>23 || c.minute>59) return false;
  const unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};
  const auto maximum=days[c.month-1]+unsigned(c.month==2 && c.year%4==0 && (c.year%100!=0 || c.year%400==0));
  return c.day>=1 && c.day<=maximum;
}
bool format_clock_sequence(const Command &command, char (&output)[33]) {
  if (!valid_clock(command.clock)) return false;
  const auto &c=command.clock;
  const int size=std::snprintf(output,sizeof output,"*6%s1%02u%02u%02u%02u%02u#", command.code.data(),
                             unsigned(c.hour),unsigned(c.minute),unsigned(c.month),unsigned(c.day),unsigned(c.year%100));
  return size>0 && unsigned(size)<sizeof output;
}
}  // namespace dsc::app
