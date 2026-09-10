// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/defines.h"
#ifdef USE_OPENGARAGE_GENERIC_SETUP
#include "generic_credentials.h"
#include "saved_wifi_reset.h"
#include "opengarage_component.h"
#include "esphome/components/api/api_server.h"
#include "esphome/components/esphome/ota/ota_esphome.h"
#include "esphome/components/web_server_base/web_server_base.h"

namespace esphome::opengarage {
class GenericSetup : public Component, public AsyncWebHandler, public ButtonRecovery {
 public:
  GenericSetup(OpenGarageComponent *parent, api::APIServer *api, esphome::ESPHomeOTAComponent *ota)
      : parent_(parent), api_(api), ota_(ota) {}
  void load_credentials();  // Codegen: after mode/threshold allocation, before App.setup().
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::BUS + 1.0f; }
  void set_status_sensor(text_sensor::TextSensor *sensor) { status_ = sensor; }
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
  // ESPAsyncWebServer otherwise discards URL-encoded POST fields before dispatch.
  bool isRequestHandlerTrivial() const override { return false; }
  void button_hold(bool factory) override;
  void button_release(bool factory) override;
 protected:
  static constexpr uint32_t WINDOW_MS = 600000;  // Initial open AP, ten minutes per power-up.
  bool trusted_host_(AsyncWebServerRequest *request) const;
  bool initial_allowed_(AsyncWebServerRequest *request) const;
  bool authorize_(AsyncWebServerRequest *request);
  bool mutation_allowed_(AsyncWebServerRequest *request) const;
  void send_(AsyncWebServerRequest *request, int code, const char *type, const std::string &body);
  void set_auth_();
  void fail_();
  OpenGarageComponent *parent_;
  api::APIServer *api_;
  esphome::ESPHomeOTAComponent *ota_;
  text_sensor::TextSensor *status_{nullptr};
  ESPPreferenceObject preference_;
  SavedWiFiReset wifi_reset_;
  GenericCredentials credentials_{};
  std::string auth_password_;  // Stable c_str until set_auth_ refreshes the middleware pointers.
  std::string token_;
  bool configured_{false};
  bool failed_{false};
  bool expired_{false};
  uint32_t started_ms_{0};
  uint32_t new_client_id_{0};
  uint32_t prepared_client_id_{0};
  bool recovery_inhibited_{false}, recovery_committed_{false}, wifi_recovery_{false};
};
}  // namespace esphome::opengarage
#endif
