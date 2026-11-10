#include "keybus.h"
#include "fake_arduino.h"
#include "powerseries.h"
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>

static_assert(!std::is_copy_constructible<dsc::Keybus>::value, "a physical Keybus owner cannot be copied");

namespace {
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void feed(dsc::Keybus &bus, const std::vector<byte> &frame) { fake::frame(frame); bus.loop(); }
void prime(dsc::Keybus &bus) { require(bus.begin(), "begin failed"); feed(bus, fixtures::status()); }
void queue_ownership_fifo() {
  dsc::Keybus bus(22,21,18); prime(bus);
  char input[] = "123";
  require(bus.enqueue(input,1) == dsc::TxAdmission::Accepted, "sequence rejected");
  input[0] = input[1] = input[2] = '9';
  const byte expected[] = {0x05,0x0A,0x0F};
  for (auto key : expected) {
    const auto wire = fake::frame(fixtures::status()); bus.loop();
    require(fake::transmitted_byte(wire,8) == key, "owned FIFO payload changed or reordered");
  }
  require(bus.pendingKeys() == 0 && bus.txOutcome() == dsc::TxOutcome::Transmitted, "no final completion");
}
void queue_capacity_atomic() {
  dsc::Keybus bus(22,21,18); prime(bus);
  char payload[dsc::TX_CAPACITY+1];
  std::fill_n(payload,dsc::TX_CAPACITY,'1'); payload[dsc::TX_CAPACITY]=0;
  require(bus.enqueue(payload,1) == dsc::TxAdmission::Accepted, "capacity-sized sequence rejected");
  require(bus.enqueue("2",1) == dsc::TxAdmission::QueueFull, "full queue not rejected");
  require(bus.pendingKeys()==dsc::TX_CAPACITY, "queue changed on rejected admission");
  bus.cancelTx();
  require(bus.enqueue("12?",1) == dsc::TxAdmission::InvalidKey, "invalid sequence admitted");
  require(bus.pendingKeys()==0, "partial invalid sequence queued");
  require(bus.enqueue("1",0)==dsc::TxAdmission::InvalidPartition, "zero partition admitted");
  require(bus.enqueue("1",9)==dsc::TxAdmission::InvalidPartition, "out-of-range partition admitted");
}
void queue_nonblocking() {
  dsc::Keybus bus(22,21,18); prime(bus);
  bus.write('1'); const auto before=micros();
  require(bus.write('2')==dsc::TxAdmission::Accepted, "second concurrent write rejected");
  require(micros()==before && bus.pendingKeys()==2, "write waited for wire progress");
}
void queue_timeout() {
  dsc::Keybus bus(22,21,18); prime(bus); bus.write("*2");
  for(unsigned i=0; i<60 && bus.pendingKeys(); ++i) {
    feed(bus,fixtures::status()); fake::advance(100000); bus.loop();
  }
  require(bus.txOutcome()==dsc::TxOutcome::TimedOut && bus.pendingKeys()==0,
          "missing star acknowledgement did not produce a bounded timeout");
  require(fake::output(18)==LOW, "timeout did not release wire");
}
void queue_disconnect() {
  dsc::Keybus bus(22,21,18); prime(bus); bus.write("123");
  fake::advance(3100000); bus.loop();
  require(bus.txOutcome()==dsc::TxOutcome::Disconnected && bus.pendingKeys()==0,
          "disconnect retained pending keys");
}
void idle_is_unknown() {
  dsc::Keybus bus(22,21,18);
  bus.loop(); require(!bus.keybusConnected, "unstarted bus reported connected");
  bus.begin(); bus.loop(); require(!bus.keybusConnected, "no-clock bus reported connected");
  require(bus.write('1')==dsc::TxAdmission::Unavailable, "unavailable bus admitted write");
}
void reset_status_preserves_observation() {
  dsc::Keybus bus(22,21,18); prime(bus);
  const bool observed=bus.disabled[0];
  bus.disabledChanged[0]=false;
  bus.resetStatus();
  require(bus.disabled[0]==observed, "resetStatus changed observed partition state");
  require(bus.disabledChanged[0], "resetStatus omitted disabled change notification");
}
void pin_configuration_lifecycle() {
  dsc::Keybus bus(22,21,18);
  require(!bus.configurePins(22,22,21), "clock/data collision accepted");
  require(bus.configurePins(23,19,19), "shared data pins rejected");
  require(bus.begin(), "configured bus failed to start");
  require(fake::attached(23) && fake::mode(19)==OUTPUT_OPEN_DRAIN, "configured GPIOs not applied");
  require(!bus.configurePins(22,21,18), "running bus allowed GPIO reassignment");
  bus.stop();
  require(bus.configurePins(22,21), "read-only pins rejected");
  require(bus.begin() && fake::attached(22), "read-only reconfiguration failed");
}
void lifecycle_owner_restart() {
  dsc::Keybus first(22,21,18); prime(first);
  dsc::Keybus second(23,19,27);
  require(!second.begin(), "two capture owners started");
  feed(first,fixtures::status(0x05,0x82));
  require(first.armed[0], "second constructor changed active GPIOs");
  first.write("*2"); first.stop();
  require(!fake::attached(22) && first.pendingKeys()==0, "stop left capture or TX active");
  require(first.begin(), "restart failed");
  feed(first,fixtures::status(0x05,0x82));
  require(first.armed[0] && first.keybusConnected, "restart lost identical first status");
  first.stop(); require(second.begin() && fake::attached(23), "ownership not released"); second.stop();
}
void zero_partition_event() {
  dsc::Keybus bus(22,21,18); prime(bus); feed(bus,fixtures::status(0x05,0x82));
  feed(bus,fake::with_checksum({0xEB,0,0,0x18,0x18,0x8A,0,0,0xE2,0}));
  require(bus.armed[0], "unpartitioned event mutated partition 1");
}
void shared_open_drain() {
  fake::configure_pins(22,21,21,true);
  dsc::Keybus bus(22,21,21); prime(bus);
  require(fake::mode(21)==OUTPUT_OPEN_DRAIN, "shared data GPIO is not open drain");
  require(fake::output(21)==HIGH, "shared data GPIO is not released when idle");
  require(bus.write('w')==dsc::TxAdmission::Accepted, "shared GPIO key rejected");
  const auto wire=fake::frame(fixtures::status()); bus.loop();
  require(fake::transmitted_byte(wire,8)==0xB1, "shared GPIO key has wrong polarity or bits");
  require(fake::output(21)==HIGH, "shared data GPIO was not released after TX");
  bus.stop(); require(fake::output(21)==HIGH, "shared data GPIO was not released on stop");
}
void reconnect_invalidates_old_zones() {
  dsc::Keybus bus(22,21,18); prime(bus); feed(bus,fixtures::zones(1,0x01));
  require((bus.snapshot().known_zone_groups & 2)!=0, "zone fixture was not marked known");
  fake::advance(3100000); bus.loop();
  require(!bus.snapshot().connected && bus.snapshot().known_zone_groups==0, "disconnect retained valid zone data");
  // The first status after reconnection is byte-for-byte identical to the
  // original status; it must not be suppressed by the old ISR cache.
  feed(bus,fixtures::status());
  require(bus.snapshot().connected && bus.snapshot().known_zone_groups==0, "reconnect reused stale zone data");
  require(bus.snapshot().partitions[0].known && bus.snapshot().partitions[0].ready,
          "reconnect discarded an unchanged first partition status");
  feed(bus,fixtures::zones(1,0));
  require((bus.snapshot().known_zone_groups & 2)!=0 && bus.snapshot().open_zones[1]==0,
          "fresh zone frame did not restore validity");
}
void reconnect_transient_partition_status() {
  dsc::Keybus bus(22,21,18); prime(bus);
  feed(bus,fixtures::status(0x05,0x82));
  require(bus.snapshot().partitions[0].armed, "armed setup failed");
  fake::advance(3100000); bus.loop();
  feed(bus,fixtures::status(0x9F,0x80));
  const auto transient=bus.snapshot();
  require(transient.connected && !transient.partitions[0].known,
          "transient access-code prompt revalidated a stale partition state");
  feed(bus,fixtures::status(0x01,0x81));
  require(bus.snapshot().partitions[0].known && !bus.snapshot().partitions[0].armed,
          "fresh steady state did not restore partition validity");
}
void initial_alarm_and_delay_status() {
  for(byte message : {byte(0x08),byte(0x0C),byte(0x11)}) {
    dsc::Keybus bus(22,21,18); require(bus.begin(),"begin failed");
    feed(bus,fixtures::status(message,0x80));
    const auto observed=bus.snapshot().partitions[0];
    require(observed.known, "explicit alarm or delay reported unknown");
    require((message==0x08 && observed.exit_delay) || (message==0x0C && observed.entry_delay) ||
            (message==0x11 && observed.alarm), "alarm or delay flag missing");
    feed(bus,fixtures::status(0x01,0x81));
    const auto restored=bus.snapshot().partitions[0];
    require(restored.known && !restored.armed && !restored.exit_delay && !restored.entry_delay && !restored.alarm,
            "steady ready status retained stale alarm or delay");
  }
}
void reconnect_clears_old_alarm_before_fresh_armed() {
  dsc::Keybus bus(22,21,18); prime(bus); feed(bus,fixtures::status(0x11,0x80));
  require(bus.snapshot().partitions[0].alarm,"alarm setup failed");
  fake::advance(3100000); bus.loop();
  feed(bus,fixtures::status(0x05,0x82));
  const auto observed=bus.snapshot().partitions[0];
  require(observed.known && observed.armed && observed.away && !observed.alarm,
          "fresh armed state retained previous alarm after reconnect");
}
void indicator_validity() {
  dsc::Keybus bus(22,21,18); require(bus.begin(),"begin failed");
  feed(bus,fixtures::status(0x3E,0x80));
  auto observed=bus.snapshot();
  require(observed.partitions[0].known && !observed.partitions[0].fire_known && !observed.trouble_known,
          "disarm message falsely reported initial indicator values");
  feed(bus,fixtures::status(0x01,0xD1));
  observed=bus.snapshot();
  require(observed.trouble_known && observed.trouble && observed.partitions[0].fire_known && observed.partitions[0].fire,
          "steady status did not establish observed indicators");
  fake::advance(3100000); bus.loop();
  feed(bus,fixtures::status(0x3E,0x80));
  observed=bus.snapshot();
  require(!observed.trouble_known && !observed.partitions[0].fire_known,
          "reconnect retained stale indicator validity");
}
void alarm_zone_change_flags() {
  dsc::Keybus bus(22,21,18); prime(bus);
  auto event=[](byte type,byte value) {
    return fake::with_checksum({0xEB,0,1,0x26,0x2B,0x6C,0,type,value,0});
  };
  for (auto item : {std::array<byte,4>{0,0x09,0x29,0},std::array<byte,4>{4,0x00,0x20,4}}) {
    const auto group=item[3];
    bus.alarmZonesChanged[group]=0; bus.alarmZonesStatusChanged=false;
    feed(bus,event(item[0],item[1]));
    require((bus.alarmZones[group]&1) && (bus.alarmZonesChanged[group]&1) && bus.alarmZonesStatusChanged,
            "zone alarm did not publish its changed bit");
    bus.alarmZonesChanged[group]=0; bus.alarmZonesStatusChanged=false;
    feed(bus,event(item[0],item[1]));
    require(!(bus.alarmZonesChanged[group]&1) && !bus.alarmZonesStatusChanged,
            "duplicate alarm event falsely reported a change");
    feed(bus,event(item[0],item[2]));
    require(!(bus.alarmZones[group]&1) && (bus.alarmZonesChanged[group]&1) && bus.alarmZonesStatusChanged,
            "zone alarm restoration did not publish its changed bit");
  }
}
void clock_and_event_are_distinct() {
  dsc::Keybus bus(22,21,18); prime(bus);
  // Panel clock 2026-10-27 12:00, then a historical event in 2018.
  feed(bus,fake::with_checksum({0xA5,0,0x26,0x2B,0x6C,0,0,0}));
  require(bus.snapshot().panel_clock.valid && bus.snapshot().panel_clock.year==2026,
          "valid panel timestamp was not recorded");
  feed(bus,fake::with_checksum({0xEB,0,1,0x18,0x18,0x8A,0,0,0xE2,0}));
  const auto snapshot=bus.snapshot();
  require(snapshot.event_clock.valid && snapshot.event_clock.year==2018 &&
          snapshot.panel_clock.valid && snapshot.panel_clock.year==2026,
          "historical event changed the panel clock");
  // Month zero has a valid checksum but is not a valid calendar timestamp.
  feed(bus,fake::with_checksum({0xA5,0,0x26,0x03,0x6C,0,0,0}));
  require(!bus.snapshot().panel_clock.valid, "invalid calendar timestamp was marked valid");
}
void emergency_repeat_before_next_key() {
  dsc::Keybus bus(22,21,18); prime(bus);
  require(bus.enqueue("f1",1)==dsc::TxAdmission::Accepted, "emergency sequence rejected");
  // F/A/P begins at the falling edge of the frame delimiter, before the first
  // sampled bit in fake::frame. One frame starts it; the next carries its last
  // seven bits. Verify those wire bits and the FIFO boundary separately.
  fake::frame(fixtures::status()); bus.loop();
  const auto first=fake::frame(fixtures::status()); bus.loop();
  require((fake::transmitted_byte(first,0)>>1)==(0xBB&0x7F) && bus.pendingKeys()==2,
          "first emergency key prematurely completed");
  const auto repeat=fake::frame(fixtures::status(1,0x81,0x1C)); bus.loop();
  require((fake::transmitted_byte(repeat,0)>>1)==(0xBB&0x7F) && bus.pendingKeys()==1,
          "emergency verification was replaced by the next key");
  const auto next=fake::frame(fixtures::status()); bus.loop();
  require(fake::transmitted_byte(next,8)==0x05 && bus.pendingKeys()==0,
          "next key did not follow emergency verification");
}
void malformed_frame_stress() {
  dsc::Keybus bus(22,21,18); prime(bus);
  std::uint32_t random=0x1A2B3C4D;
  auto next=[&]() { random^=random<<13; random^=random>>17; random^=random<<5; return random; };
  constexpr byte commands[]={0x05,0x1B,0x16,0x27,0x2D,0x34,0x3E,0x87,0xA5,0xE6,0xEB};
  for (unsigned n=0;n<2000;++n) {
    const auto size=2+(next()%15);
    std::vector<byte> bytes(size);
    for (auto &value:bytes) value=static_cast<byte>(next());
    bytes[0]=commands[next()%(sizeof commands)]; bytes[1]=0;
    const auto command=bytes[0];
    if (command!=0x05 && command!=0x1B) {
      unsigned sum=0; for (unsigned i=0;i+1<size;++i) if(i!=1) sum+=bytes[i];
      bytes[size-1]=static_cast<byte>(sum^0x80);
    }
    const auto before=bus.snapshot().version;
    feed(bus,bytes);
    if (command!=0x05 && command!=0x1B)
      require(bus.snapshot().version==before,"invalid checksum changed observed state");
  }
}
}

std::map<std::string,std::function<void()>> core_regressions() {
  return {{"owned-queue-payload-fifo",queue_ownership_fifo},
          {"owned-queue-capacity-atomic",queue_capacity_atomic},
          {"owned-queue-nonblocking",queue_nonblocking},
          {"owned-queue-timeout",queue_timeout},
          {"owned-queue-disconnect",queue_disconnect},
          {"owned-idle-is-unknown",idle_is_unknown},
          {"owned-reset-preserves-observation",reset_status_preserves_observation},
          {"owned-pin-configuration-lifecycle",pin_configuration_lifecycle},
          {"owned-lifecycle-owner-restart",lifecycle_owner_restart},
          {"owned-zero-partition-event",zero_partition_event},
          {"owned-shared-open-drain",shared_open_drain},
          {"owned-reconnect-invalidates-zones",reconnect_invalidates_old_zones},
          {"owned-reconnect-transient-partition",reconnect_transient_partition_status},
          {"owned-initial-alarm-and-delay",initial_alarm_and_delay_status},
          {"owned-reconnect-clears-old-alarm",reconnect_clears_old_alarm_before_fresh_armed},
          {"owned-indicator-validity",indicator_validity},
          {"owned-alarm-zone-change-flags",alarm_zone_change_flags},
          {"owned-clock-and-event-distinct",clock_and_event_are_distinct},
          {"owned-emergency-repeat-order",emergency_repeat_before_next_key},
          {"owned-malformed-frame-stress",malformed_frame_stress}};
}
