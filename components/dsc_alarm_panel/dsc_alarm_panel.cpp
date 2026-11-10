// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsc_alarm_panel.h"
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"
#ifdef DSC_ENABLE_REPLAY
#include <esp_heap_caps.h>
#endif

namespace esphome::dsc_alarm_panel {
static const char *const TAG = "dsc_alarm_panel";
void DscAlarmPanel::register_zone(std::uint8_t number,const char *name,const char *group,const char *category) {
  if (!service_.configure_zone(number,name,group,category))
    ESP_LOGE(TAG,"Invalid or duplicate zone metadata for zone %u",unsigned(number));
}
void DscAlarmPanel::setup() {
  bool started=false;
#ifdef DSC_ENABLE_REPLAY
  if (simulation_) started=simulator_.begin();
#endif
  if (physical_bus_) started=bus_.configurePins(clock_pin_,read_pin_,write_pin_) && bus_.begin();
  if ((simulation_ || physical_bus_) && !started) {
    ESP_LOGE(TAG, "Cannot initialize requested Keybus transport");
    mark_failed();
  }
  service_.tick(millis());
  ESP_LOGI(TAG, "Transport: %s", simulation_ ? "SIMULATION" : physical_bus_ ? "physical" : "idle");
}
void DscAlarmPanel::loop() {
  const auto now=millis();
  if (bus_.running()) {
    bus_.loop();
#ifdef DSC_ENABLE_REPLAY
    if (simulation_) {
      const auto online=pending_online_.exchange(0);
      if (online) simulator_.set_online(online==2);
      for (unsigned i=0;i<pending_zones_.size();++i) {
        const auto action=pending_zones_[i].exchange(0);
        if (action) simulator_.set_zone(i+1,action==2);
      }
      simulator_.step(now);
      if (static_cast<std::uint32_t>(now-last_metrics_)>=60000) {
        last_metrics_=now;
        ESP_LOGI(TAG,"bench_metrics uptime_ms=%u free=%u largest=%u",unsigned(now),
                 unsigned(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                 unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
      }
    }
#endif
    service_.tick(now);
  }
  if (!network_reported_ && network::is_connected()) {
    char address[network::IP_ADDRESS_BUFFER_SIZE];
    ESP_LOGI(TAG, "Device IP: %s", network::get_ip_addresses()[0].str_to(address));
    network_reported_=true;
  }
}
#ifdef DSC_ENABLE_REPLAY
bool DscAlarmPanel::schedule_simulated_zone(std::uint8_t zone,bool open) {
  if (!simulation_ || zone<1 || zone>64) return false;
  pending_zones_[zone-1].store(open ? 2 : 1); return true;
}
bool DscAlarmPanel::schedule_simulated_online(bool online) {
  if (!simulation_) return false;
  pending_online_.store(online ? 2 : 1); return true;
}
#endif
void DscAlarmPanel::dump_config() {
  ESP_LOGCONFIG(TAG, "DSC prototype: %s", simulation_ ? "SIMULATION" : physical_bus_ ? "physical" : "idle");
}
}  // namespace esphome::dsc_alarm_panel
