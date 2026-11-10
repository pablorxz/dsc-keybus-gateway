// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/component.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/components/dsc_alarm_panel/dsc_alarm_panel.h"
#include "digest_nonce.h"

namespace esphome::dsc_web {
class DscWeb : public Component, public AsyncWebHandler {
 public:
  void set_parent(dsc_alarm_panel::DscAlarmPanel *parent) { parent_=parent; }
  void set_base(web_server_base::WebServerBase *base) { base_=base; }
  void set_auth(const char *username,const char *password) { username_=username; password_=password; }
  // The IDF HTTP server creates sockets, so start it after Wi-Fi initializes
  // lwIP. Component's default DATA priority runs before the Wi-Fi setup.
  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }
  void setup() override;
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
  bool isRequestHandlerTrivial() const override { return false; }
 protected:
  dsc_alarm_panel::DscAlarmPanel *parent_{nullptr};
  web_server_base::WebServerBase *base_{nullptr};
  const char *username_{nullptr}, *password_{nullptr};
  DigestNonces nonces_{};
  bool authenticate(AsyncWebServerRequest *request);
  void challenge(AsyncWebServerRequest *request, bool stale=false);
  void state(AsyncWebServerRequest *request);
  void operation(AsyncWebServerRequest *request);
  void operation_code(AsyncWebServerRequest *request);
  void activity(AsyncWebServerRequest *request);
  void command(AsyncWebServerRequest *request);
  void simulation(AsyncWebServerRequest *request, bool zone);
};
}  // namespace esphome::dsc_web
