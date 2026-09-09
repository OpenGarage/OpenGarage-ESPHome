// SPDX-License-Identifier: GPL-3.0-or-later
#include "esphome/core/defines.h"
#ifdef USE_OPENGARAGE_THRESHOLDS
#include "opengarage_component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::opengarage {
static const char *const THRESHOLD_TAG = "opengarage.threshold";

void OpenGarageComponent::load_threshold_settings() {
  if (thresholds_loaded_) return;
  thresholds_loaded_ = true;
  if (global_preferences == nullptr) return;
  // Explicit flash storage even in profiles whose default preferences use RTC.
  // Codegen allocates this AFTER the existing unified-mode record, before setup.
  threshold_preference_ = global_preferences->make_preference<uint32_t>(ThresholdSettings::KEY, true);
  uint32_t stored;
  ThresholdSettings settings;
  if (threshold_preference_.load(&stored)) {
    if (settings.decode(stored)) {
      config_.door_threshold_cm = settings.door;
      config_.vehicle_threshold_cm = settings.vehicle;
    } else ESP_LOGW(THRESHOLD_TAG, "Invalid stored thresholds; using YAML defaults");
  }
}

void OpenGarageComponent::publish_thresholds_() {
  if (door_threshold_number_) door_threshold_number_->publish_state(config_.door_threshold_cm);
  if (vehicle_threshold_number_) vehicle_threshold_number_->publish_state(config_.vehicle_threshold_cm);
}

bool OpenGarageComponent::threshold_editable_(bool door) const {
  if (!thresholds_ready_ || thresholds_stopped_ || !config_.distance_enabled) return false;
  if (door) {
    if (!door_threshold_number_ || config_.source == StateSource::CONTACT || config_.source == StateSource::PROTOCOL)
      return false;
  } else if (!vehicle_threshold_number_ || config_.mounting == Mounting::SIDE) return false;
#ifdef USE_OPENGARAGE_UNIFIED
  if (configuration_stopped_ || unified_ota_latched_) return false;
#endif
#if defined(USE_OPENGARAGE_PULSE_MVP) || defined(USE_OPENGARAGE_SECPLUS1_CONTROL) || defined(USE_OPENGARAGE_SECPLUS2_CONTROL)
  if (update_gate_.open()) return false;
#endif
#ifdef USE_OPENGARAGE_CONTROL
  // Do not truncate an already dispatched contact or interrupt protocol release
  // cleanup to write flash. A valid change during WARNING cancels it below.
  if (action_controller_.phase() == ActionPhase::PULSING) return false;
#endif
#if defined(USE_OPENGARAGE_SECPLUS1_CONTROL) || defined(USE_OPENGARAGE_SECPLUS2_CONTROL)
  if (light_intent_.waiting() || lock_intent_.waiting()) return false;
#endif
#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
  if (secplus1_selected_() && secplus1_.releasing()) return false;
#endif
#ifdef USE_OPENGARAGE_SECPLUS2_CONTROL
  if (secplus2_selected_() && secplus2_.releasing()) return false;
#endif
  return true;
}

void OpenGarageComponent::request_threshold(bool door, float value) {
  if (!ThresholdSettings::valid_input(door, value) || !threshold_editable_(door)) {
    ESP_LOGW(THRESHOLD_TAG, "Threshold unchanged: invalid, unavailable, busy or updating");
    publish_thresholds_(); return;
  }
  ThresholdSettings before{config_.door_threshold_cm, config_.vehicle_threshold_cm}, next = before;
  (door ? next.door : next.vehicle) = static_cast<uint16_t>(value);
  if (next.encode() == before.encode()) { publish_thresholds_(); return; }
#ifdef USE_OPENGARAGE_CONTROL
  if (action_controller_.phase() == ActionPhase::WARNING) {
    // Cancel BEFORE ticking any due warning, including exactly at its deadline.
    action_controller_.cancel(millis());
    publish_control_();
  }
#endif
  const uint32_t encoded = next.encode();
  const bool saved = threshold_preference_.save(&encoded);
  const bool synced = saved && global_preferences != nullptr && global_preferences->sync();
  if (!synced) {
    // Best-effort restore of the RAM cache so a later unrelated sync does not
    // commit a rejected change. A failed flash write is never claimed successful.
    if (saved) { const uint32_t old = before.encode(); threshold_preference_.save(&old); }
    ESP_LOGW(THRESHOLD_TAG, "Threshold save failed; retaining current values; retry when idle");
    publish_thresholds_(); return;
  }
  config_.door_threshold_cm = next.door;
  config_.vehicle_threshold_cm = next.vehicle;
  ESP_LOGI(THRESHOLD_TAG, "Saved thresholds: door %u cm, vehicle %u cm", next.door, next.vehicle);
  publish_thresholds_();
  // Publish freshly resolved readings without calling the action controller or
  // generating any command. Normal loop processing resumes with the new values.
  publish_(millis());
}
}  // namespace esphome::opengarage
#endif
