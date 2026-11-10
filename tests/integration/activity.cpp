#include "activity.h"
#include <cassert>
#include <iostream>
using namespace dsc;
using namespace dsc::app;
int main() {
  ActivityLog log; ActivitySnapshot out; PanelState s;
  s.connected=true; s.known_zone_groups=1; s.partitions[0].known=true;
  log.observe(s,0xfffffff0u); log.copy_to(out); assert(out.count==0);
  s.open_zones[0]=1; log.observe(s,5); log.copy_to(out);
  assert(out.count==1 && out.events[0].kind==ActivityKind::ZoneOpen && out.events[0].number==1 && out.now_ms==21);
  s.connected=false; log.observe(s,10); s.open_zones[0]=0; log.observe(s,20);
  s.connected=true; log.observe(s,30); log.copy_to(out);
  assert(out.count==3 && out.events[0].kind==ActivityKind::Connected && out.events[1].kind==ActivityKind::Disconnected);
  s.known_zone_groups=0; s.open_zones[0]=1; log.observe(s,40);
  s.known_zone_groups=1; log.observe(s,50); log.copy_to(out); assert(out.count==3);
  for(int i=0;i<70;++i) { s.open_zones[0]^=1; log.observe(s,60+i); }
  log.copy_to(out); assert(out.count==64 && out.events[0].kind==ActivityKind::ZoneOpen);
  std::cout<<"PASS bounded observed activity, unknown states and clock wrap\n";
}
