// SPDX-License-Identifier: GPL-3.0-or-later
#include "esphome/core/defines.h"
#ifdef USE_OPENGARAGE_UNIFIED
#include "opengarage_component.h"
#include <cstdio>
#include "esphome/core/log.h"

namespace esphome::opengarage {
void OpenGarageComponent::load_unified_settings(OpenerProtocol protocol, PanelEmulation panel) {
  if (settings_loaded_) return;
  settings_loaded_ = true;
  active_settings_ = {protocol, panel};
  if (global_preferences == nullptr) {
    active_settings_ = {}; settings_error_ = true;
  } else {
    // Dedicated, versioned key. Independent of Sec+2 identity/counter and of
    // the order/names of HA entities. One atomic checksummed preference record.
    mode_preference_ = global_preferences->make_preference<uint32_t>(0x4F475501U, true);
    uint32_t stored;
    if (mode_preference_.load(&stored) && !active_settings_.decode(stored)) {
      active_settings_ = {}; settings_error_ = true;
    }
  }
  pending_settings_ = active_settings_;
}

void OpenGarageComponent::stop_unified_() {
  action_controller_.shutdown();
  light_intent_.cancel(); lock_intent_.cancel();
  secplus1_stopped_ = secplus2_stopped_ = true;
  if (secplus1_selected_()) secplus1_.stop();
  if (secplus2_selected_()) secplus2_.stop();
}

void OpenGarageComponent::publish_settings_() {
  if (protocol_select_) protocol_select_->publish_state(size_t(pending_settings_.protocol));
  if (panel_select_) panel_select_->publish_state(size_t(pending_settings_.panel));
  if (configuration_text_) {
    char text[160];
    std::snprintf(text, sizeof(text), "Active: %s; %s", protocol_name(active_settings_.protocol),
        settings_error_ ? "settings error; check after restart" : configuration_stopped_ ?
        "settings saved; Restart Device required" : active_settings_.protocol == OpenerProtocol::UNCONFIGURED ?
        "select protocol, then Restart Device" : "configuration applied");
    configuration_text_->publish_state(text);
  }
}

void OpenGarageComponent::request_setting(bool panel, size_t index) {
  if (!settings_loaded_ || index > (panel ? 1U : 3U)) return;
  // Do not change/flush settings during OTA. Stopped flags also cover native
  // OTA, which need not have entered the browser upload gate.
  if (update_gate_.open() || unified_ota_latched_) return;
  auto next = pending_settings_;
  if (panel) next.panel = PanelEmulation(index);
  else next.protocol = OpenerProtocol(index);
  if (next == pending_settings_ && !settings_error_) { publish_settings_(); return; }
  // Stop accepted work before persisting. A configuration change must not let
  // a previously accepted command survive into a different interpretation.
  stop_unified_();
  configuration_stopped_ = true;
  const uint32_t encoded = next.encode();
  const bool saved = mode_preference_.save(&encoded);
  const bool synced = saved && global_preferences != nullptr && global_preferences->sync();
  settings_error_ = !synced;
  if (synced) pending_settings_ = next;
  // On error, persistence is uncertain: stay stopped, do not advertise success
  // or auto-reboot. The owner can retry or restart and inspect the loaded mode.
  publish_settings_(); publish_control_();
}
}  // namespace esphome::opengarage
#endif
