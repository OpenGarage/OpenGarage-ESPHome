// SPDX-License-Identifier: GPL-3.0-or-later
#include "generic_setup.h"
#ifdef USE_OPENGARAGE_GENERIC_SETUP
#include "esphome/core/alloc_helpers.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/captive_portal/captive_portal.h"
#include "esphome/components/wifi/wifi_component.h"
#include "generic_setup_page.h"
#include <cstdio>

namespace esphome::opengarage {
namespace {
std::string hex_bytes(const uint8_t *bytes, size_t size) {
  static constexpr char HEX_DIGITS[] = "0123456789abcdef";
  std::string result;
  result.reserve(size * 2);
  for (size_t i = 0; i < size; ++i) { result += HEX_DIGITS[bytes[i] >> 4]; result += HEX_DIGITS[bytes[i] & 15]; }
  return result;
}
std::string form_value(AsyncWebServerRequest *request, const char *key) {
  // POST body only: credentials must not be accepted from query strings.
  return request->hasParam(key, true) ? request->getParam(key, true)->value().c_str() : "";
}
}

void GenericSetup::load_credentials() {
  if (global_preferences != nullptr) {
    preference_ = global_preferences->make_preference<GenericCredentials>(GenericCredentials::MAGIC, true);
    configured_ = preference_.load(&credentials_) && credentials_.valid();
    wifi_reset_.bind_next_slot();  // Alias only: preserves the following Wi-Fi/API allocation order.
    setup_tune_preference_ = global_preferences->make_preference<uint32_t>(SETUP_TUNE_MARKER, false);
    uint32_t marker=0, clear=0;
    if (setup_tune_preference_.load(&marker) && marker==SETUP_TUNE_MARKER)
      setup_tune_pending_ = setup_tune_preference_.save(&clear); // Consume once across the setup's soft reboot.
  }
  if (configured_) {
    parent_->set_unified_client(credentials_.client_id);
    wifi_recovery_ = !wifi_reset_.has_saved_network();
    if (wifi_recovery_) parent_->inhibit_for_setup(); // AP-only recovery is not offline door operation.
  }
  else { credentials_ = {}; parent_->inhibit_for_setup(); }
}

void GenericSetup::fail_() {
  failed_ = true;
  parent_->inhibit_for_setup();
  // Called before listener setup. The first HTTP handler also denies all
  // requests, including upload bodies, if the entropy/storage initialization fails.
  api_->mark_failed(); ota_->mark_failed();
  if (status_) status_->publish_state("Setup error; controls inhibited; serial recovery may be required");
  ESP_LOGE("og.setup", "Credential initialization failed; networking controls inhibited");
}

void GenericSetup::set_auth_() {
  auto *base = web_server_base::global_web_server_base;
  base->set_auth_username("admin");
  base->set_auth_password(auth_password_.c_str());
  ota_->set_auth_password(auth_password_);
}

void GenericSetup::setup() {
  started_ms_ = millis();
  parent_->set_button_recovery(this);
  auto *base = web_server_base::global_web_server_base;
  // Register BEFORE the captive portal and upstream web/OTA handlers. We handle
  // our own auth because initial setup is the sole intentionally public route.
  base->add_handler_without_auth(this);
  uint8_t entropy[64];
  if (!random_bytes(entropy, sizeof(entropy))) { fail_(); return; }
  token_ = hex_bytes(entropy, 16);
  if (configured_) auth_password_ = credentials_.admin;
  else {
    // Undisclosed per-boot authentication for ALL other listeners. Neither the
    // empty YAML password nor the well-known zero API key ever becomes reachable.
    auth_password_ = hex_bytes(entropy + 16, 32);
    // Separate 128-bit generated admin password; disclose only in the AP's
    // protected POST preview. Other listeners retain the undisclosed boot secret
    // until the installer confirms the preview and the record commits.
    const auto admin = hex_bytes(entropy + 48, 16);
    std::memcpy(credentials_.admin, admin.c_str(), admin.size() + 1);
    if (!random_bytes(credentials_.api_key.data(), credentials_.api_key.size()) ||
        api::APINoiseContext::is_all_zeros(credentials_.api_key)) { fail_(); return; }
  }
  set_auth_();
  api_->set_noise_psk(credentials_.api_key);
  if (configured_) {
    auto ap = wifi::global_wifi_component->get_ap();
    ap.set_password(credentials_.admin);
    wifi::global_wifi_component->set_ap(ap);
  }
  if (status_) status_->publish_state(configured_ ? (wifi_recovery_ ?
      "Wi-Fi recovery AP; sign in as admin and configure Wi-Fi; outputs inhibited" : "Configured; admin setup page: /og/setup") :
      "Setup required; open setup AP within 10 minutes; outputs inhibited");
}

void GenericSetup::loop() {
  if (!configured_ && !expired_ && uint32_t(millis() - started_ms_) >= WINDOW_MS) {
    expired_ = true;
    // Stop the open AP, not merely its HTML form. No automatic reboot reopens it.
    wifi::global_wifi_component->disable();
    if (status_) status_->publish_state("Setup window closed; power-cycle to reopen; outputs inhibited");
  }
  auto *wifi=wifi::global_wifi_component;
  const bool allowed=!failed_ && !expired_ && !recovery_inhibited_ && !recovery_committed_ && wifi;
  bool station=false;
  if (allowed && configured_ && !parent_->setup_inhibited() && wifi->is_connected()) {
    for (const auto &ip : wifi->get_ip_addresses()) if (ip.is_ip4() && ip.is_set()) { station=true; break; }
  }
  const bool ap=allowed && wifi->is_ap_active();
  if (station && !station_announced_) {
    station_announced_=true; // If a warning/IP report owns the buzzer, skip rather than queue.
    parent_->startup_tune(setup_tune_pending_?StartupTune::SETUP_SUCCESS:StartupTune::STATION);
    setup_tune_pending_=false;
  } else if (!station && ap && !ap_announced_) {
    ap_announced_=true;
    parent_->startup_tune(StartupTune::AP);
  }
  parent_->service_startup_audio(allowed && (station || ap));
}

void GenericSetup::button_report_ip() {
  auto *wifi = wifi::global_wifi_component;
  if (!configured_ || recovery_inhibited_ || !wifi || !wifi->is_connected()) return;
  for (const auto &ip : wifi->get_ip_addresses()) {
    if (!ip.is_ip4() || !ip.is_set()) continue;
    char address[64]; // Larger than ESPHome's IPv4/IPv6 formatting buffer requirement.
    ip.str_to(address);
    parent_->report_ip(address);
    return;
  }
}

void GenericSetup::button_hold(bool factory) {
  if (recovery_committed_) return;
  if (!parent_->button_recovery_allowed()) {
    if (status_) status_->publish_state("Reset ignored during firmware upload; wait for completion");
    return;
  }
  recovery_inhibited_ = true;
  parent_->button_recovery_feedback(factory); // Stop controls/transports before buzzer feedback.
  if (status_) status_->publish_state(factory ? "Release button for factory reset; all settings will be erased" :
      "Release button for Wi-Fi AP reset; hold past 9.5 seconds for factory reset");
}

void GenericSetup::button_release(bool factory) {
  if (recovery_committed_) return;
  if (!parent_->button_recovery_allowed()) {
    if (status_) status_->publish_state("Reset ignored during firmware upload; wait for completion");
    return;
  }
  recovery_inhibited_ = true;
  parent_->inhibit_for_setup(); // Also covers release without a preceding threshold tick.
  const bool ok = global_preferences && (factory ? global_preferences->reset() : wifi_reset_.clear());
  if (!ok) {
    if (status_) status_->publish_state("Reset save failed; outputs inhibited; retry or recover over USB");
    ESP_LOGE("og.setup", "Physical reset failed; controls remain inhibited; not restarting");
    return;
  }
  recovery_committed_ = true;
  ESP_LOGI("og.setup", "Physical %s reset saved; restarting", factory ? "factory" : "Wi-Fi AP");
  // ESP8266 reset() prevents preference writes until restart, so shutdown cannot
  // resurrect erased records. AP reset uses an ordinary checked save/sync.
  App.safe_reboot();
}

bool GenericSetup::canHandle(AsyncWebServerRequest *request) const {
  const auto &url = request->url();
  // No stock captive-portal GET /wifisave bypass, even during later AP recovery.
  // While unconfigured, consume EVERYTHING (including /update upload bodies).
  return recovery_inhibited_ || failed_ || !configured_ || url.startsWith("/og/") || url == "/wifisave" || url == "/config.json" ||
      (wifi::global_wifi_component->is_ap_active() && request->method() == HTTP_GET);
}

bool GenericSetup::trusted_host_(AsyncWebServerRequest *request) const {
  const String ip = request->client()->localIP().toString();
  const String name = App.get_name().c_str();
  const String host = request->host();
  // Reject attacker-controlled DNS-rebinding Host values. Explicit :80 is fine.
  return host == ip || host == ip + ":80" || host == name + ".local" || host == name + ".local:80";
}

bool GenericSetup::initial_allowed_(AsyncWebServerRequest *request) const {
  return !failed_ && !configured_ && !expired_ && uint32_t(millis() - started_ms_) < WINDOW_MS &&
      wifi::global_wifi_component->is_ap_active() &&
      request->client()->localIP().toString() == "192.168.4.1" && trusted_host_(request);
}

bool GenericSetup::authorize_(AsyncWebServerRequest *request) {
  if (!trusted_host_(request)) { send_(request, 403, "text/plain", "Use the device IP or its .local hostname."); return false; }
  if (configured_) {
    if (request->authenticate("admin", auth_password_.c_str())) return true;
    request->requestAuthentication(nullptr, true);
    return false;
  }
  if (initial_allowed_(request)) return true;
  send_(request, 403, "text/plain", "Initial setup requires the device setup AP within 10 minutes of power-up.");
  return false;
}

bool GenericSetup::mutation_allowed_(AsyncWebServerRequest *request) const {
  // A browser-origin check plus a random per-boot token. Accept neither query
  // string passwords nor cross-origin forms, including opaque/null origins.
  return request->contentLength() <= 1024 && request->hasHeader("Origin") &&
      request->getHeader("Origin")->value() == "http://" + request->host() &&
      request->hasHeader("X-OG-CSRF") && request->getHeader("X-OG-CSRF")->value() == token_.c_str();
}

void GenericSetup::send_(AsyncWebServerRequest *request, int code, const char *type, const std::string &body) {
  auto *response = request->beginResponse(code, type, body.c_str());
  response->addHeader("Cache-Control", "no-store");
  response->addHeader("X-Content-Type-Options", "nosniff");
  response->addHeader("X-Frame-Options", "DENY");
  response->addHeader("Referrer-Policy", "no-referrer");
  request->send(response);
}

void GenericSetup::handleRequest(AsyncWebServerRequest *request) {
  if (recovery_inhibited_) { send_(request, 503, "text/plain", "Physical reset in progress or failed; controls inhibited. Restart or retry the physical reset."); return; }
  if (failed_) { send_(request, 503, "text/plain", "Setup initialization failed; serial recovery required."); return; }
  if (!authorize_(request)) return;
  const auto &url = request->url();
  if (url == "/wifisave") { send_(request, 405, "text/plain", "Use the authenticated setup form."); return; }
  if (request->method() == HTTP_GET) {
    if (url == "/og/info") {
      const std::string mac = get_mac_address_pretty();
      const std::string mac_suffix = mac.substr(12, 2) + mac.substr(15, 2);
      char id[11]; std::snprintf(id, sizeof(id), "%lu", (unsigned long) credentials_.client_id);
      const std::string key = configured_ ? base64_encode(credentials_.api_key.data(), credentials_.api_key.size()) : "";
      send_(request, 200, "application/json", std::string("{\"configured\":") + (configured_ ? "true" : "false") +
          // Match canHandle(): while the AP is active, GET / serves setup, not the dashboard.
          ",\"dashboard_available\":" + (configured_ && !wifi::global_wifi_component->is_ap_active() ? "true" : "false") +
          ",\"hostname\":\"" + App.get_name().c_str() + ".local\"" +
          ",\"mac_suffix\":\"" + mac_suffix + "\"" +
          ",\"token\":\"" + token_ + "\",\"key\":\"" + key + "\",\"client_id\":\"" + (configured_ ? id : "") + "\"}");
    } else if (url == "/config.json") {
      captive_portal::global_captive_portal->handle_config(request);  // Standard scan results/escaping.
    } else {
      auto *response = request->beginResponse_P(200, "text/html", GENERIC_SETUP_PAGE);
      response->addHeader("Cache-Control", "no-store");
      response->addHeader("X-Frame-Options", "DENY");
      response->addHeader("Referrer-Policy", "no-referrer");
      request->send(response);
    }
    return;
  }
  if (request->method() != HTTP_POST || !mutation_allowed_(request)) {
    send_(request, 403, "text/plain", "Reload the setup page and submit from this device's page."); return;
  }
  if (url == "/og/update" && configured_) {
    parent_->enter_update_mode();
    send_(request, 200, "text/plain", "Firmware update mode entered. Upload a matching application .bin below, or restart the device to resume controls.");
    return;
  }
  const bool preview = url == "/og/prepare";
  if (!preview && url != "/og/setup" && url != "/og/wifi") { send_(request, 404, "text/plain", "Not found"); return; }
  if ((url != "/og/wifi") == configured_) { send_(request, 409, "text/plain", "Initial credentials cannot be replaced here."); return; }
  if (!request->hasParam("ssid", true) || !request->hasParam("wifi_password", true)) {
    send_(request, 400, "text/plain", "Setup form fields are missing. Reload the page and try again."); return;
  }
  const std::string ssid = form_value(request, "ssid"), wifi_password = form_value(request, "wifi_password");
  if (ssid.empty() || ssid.size() > 32 || ssid.find('\0') != std::string::npos) {
    send_(request, 400, "text/plain", "Choose a Wi-Fi network or enter its name (1-32 bytes)."); return;
  }
  if ((!wifi_password.empty() && (wifi_password.size() < 8 || wifi_password.size() > 63)) ||
      wifi_password.find('\0') != std::string::npos) {
    send_(request, 400, "text/plain", "Wi-Fi password must be 8-63 characters, or empty for an open network. This checks the format, not whether the router accepts it."); return;
  }
  if (!configured_) {
    GenericCredentials pending = credentials_;
    // Identity is automatic, not user input. Reject obsolete/custom forms rather
    // than pretending that a supplied identity was imported.
    if (request->hasParam("identity", true) || request->hasParam("client_id", true)) {
      send_(request, 400, "text/plain", "Client identity is automatic. Reload the setup page."); return;
    }
    if (new_client_id_ == 0) {
      uint32_t generated = 0;
      if (!random_bytes(reinterpret_cast<uint8_t *>(&generated), sizeof(generated))) {
        send_(request, 503, "text/plain", "Could not generate identity. Retry setup."); return;
      }
      new_client_id_ = secplus2_client_id_from_random(generated);
    }
    // Repeated previews reuse this ID. It joins the existing fixed-size record
    // on confirmation; normal boot/OTA/Wi-Fi reset load it without regeneration.
    pending.client_id = new_client_id_;
    if (preview) {
      // No flash/Wi-Fi changes here. Give the installer time to save credentials
      // before the AP disappears (captive browsers may close on connection).
      prepared_client_id_ = pending.client_id;
      char id[11]; std::snprintf(id, sizeof(id), "%lu", (unsigned long) pending.client_id);
      send_(request, 200, "application/json", std::string("{\"admin\":\"") + pending.admin +
          "\",\"key\":\"" + base64_encode(pending.api_key.data(), pending.api_key.size()) +
          "\",\"client_id\":\"" + id + "\"}");
      return;
    }
    if (prepared_client_id_ == 0 || prepared_client_id_ != pending.client_id) {
      send_(request, 409, "text/plain", "Review and save the generated device credentials before connecting."); return;
    }
    if (!pending.valid() || !preference_.save(&pending) || !global_preferences->sync()) {
      // Never retry provisioning in this boot after an ambiguous commit. A later
      // sync must not silently commit a different newly generated opener identity.
      fail_(); send_(request, 503, "text/plain", "Credential write failed. Power-cycle and check before retrying."); return;
    }
    credentials_ = pending;
    configured_ = true;
    auth_password_ = credentials_.admin;
    set_auth_();
    if (status_) status_->publish_state("Setup saved; restarting; outputs remain inhibited until restart");
  }
  // ESPHome's standard saved Wi-Fi backend, not an OG-specific Wi-Fi store.
  wifi::global_wifi_component->save_wifi_sta(ssid, wifi_password);
  if (wifi::global_wifi_component->is_ap_active()) {
    const uint32_t marker=SETUP_TUNE_MARKER;
    if (!setup_tune_preference_.save(&marker)) ESP_LOGW("og.setup", "Setup melody marker unavailable; using normal boot tune");
  }
  send_(request, 200, "text/plain", "Wi-Fi settings saved; restarting. Connection to the new network is not yet confirmed. Join that network, then open the device address below. If Wi-Fi fails, reconnect to the setup AP using the saved admin password.");
  set_timeout("setup-restart", 3000, []() { App.safe_reboot(); });
}
}  // namespace esphome::opengarage
#endif
