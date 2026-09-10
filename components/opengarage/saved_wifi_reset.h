// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/preferences.h"
#include "esphome/components/esp8266/preference_backend.h"
#include "esphome/components/wifi/wifi_component.h"
#include <cstring>

namespace esphome::opengarage {
// ESPHome 2026.8.2 / ESP8266 slot-backed Wi-Fi preference adapter. No allocation,
// record insertion, whole-sector reset, or raw flash writes. Bind immediately
// after OG credentials: WiFiComponent::start() allocates the next flash slot.
// Its public concrete backend permits an alias of that slot. Validate the loaded
// record against Wi-Fi's active configuration before invalidating it.
class SavedWiFiReset {
 public:
  static constexpr uint32_t WIFI_TYPE = 88491487UL; // Wi-Fi with no compiled STA.
  static constexpr uint8_t WIFI_WORDS = (sizeof(wifi::SavedWifiSettings) + 3) / 4;
  static_assert(sizeof(wifi::SavedWifiSettings) == 98, "Re-audit Wi-Fi preference layout");
  void bind_next_slot() {
    if (!global_preferences || global_preferences->current_flash_offset + WIFI_WORDS + 1 > 128) return;
    backend_.offset = global_preferences->current_flash_offset;
    backend_.length_words = WIFI_WORDS;
    backend_.type = WIFI_TYPE;
    backend_.in_flash = true;
    bound_ = true;
  }
  bool has_saved_network() {
    wifi::SavedWifiSettings saved{};
    return bound_ && backend_.load(reinterpret_cast<uint8_t *>(&saved), sizeof(saved)) &&
        saved.ssid[0] != 0 && std::memchr(saved.ssid, 0, sizeof(saved.ssid)) &&
        std::memchr(saved.password, 0, sizeof(saved.password));
  }
  bool clear() {
    auto *wifi = wifi::global_wifi_component;
    if (!bound_ || !global_preferences || !wifi ||
        global_preferences->current_flash_offset < backend_.offset + WIFI_WORDS + 1U) return false;
    wifi::SavedWifiSettings saved{};
    const bool loaded = backend_.load(reinterpret_cast<uint8_t *>(&saved), sizeof(saved));
    if (!loaded) return !wifi->has_sta(); // Already AP-only; never guess a different occupied slot.
    if (!std::memchr(saved.ssid, 0, sizeof(saved.ssid)) ||
        !std::memchr(saved.password, 0, sizeof(saved.password)) || !wifi->has_sta()) return false;
    const auto sta = wifi->get_sta();
    if (sta.get_ssid() != saved.ssid || sta.get_password() != saved.password) return false;
    // A VALID empty SSID record is a wildcard for open networks upstream. Write
    // all-zero data AND a deliberately different checksum salt instead. The
    // upstream XOR-seeded checksum for zero data is WIFI_TYPE, never zero, so
    // Wi-Fi rejects this record and boots immediately into its normal AP path.
    auto erased = backend_;
    erased.type = 0;
    wifi::SavedWifiSettings empty{};
    return erased.save(reinterpret_cast<const uint8_t *>(&empty), sizeof(empty)) && global_preferences->sync();
  }
 protected:
  esp8266::ESP8266PreferenceBackend backend_;
  bool bound_{false};
};
}  // namespace esphome::opengarage
