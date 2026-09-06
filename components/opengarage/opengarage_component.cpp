// SPDX-License-Identifier: GPL-3.0-or-later
#include "opengarage_component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <cmath>
#ifdef USE_OPENGARAGE_CONTROL
#include "pulse_hardware.h"
#include "esphome/core/alloc_helpers.h"
#include "esphome/components/wifi/wifi_component.h"
#endif

namespace esphome::opengarage {
static const char *const TAG = "opengarage";

static void publish_binary(binary_sensor::BinarySensor *sensor, std::optional<bool> state) {
  if (!sensor) return;
  if (state) sensor->publish_state(*state);
  else if (sensor->has_state()) sensor->invalidate_state();
}

static void publish_text(text_sensor::TextSensor *sensor, const char *state) {
  if (sensor && (!sensor->has_state() || sensor->state != state)) sensor->publish_state(state);
}

void OpenGarageComponent::setup() {
  // GPIO15/GPIO5/GPIO13 are deliberately not configured or driven in M1.
  button_pin_->setup();
  capability_pin_->setup();
  led_pin_->pin_mode(gpio::FLAG_INPUT);
  if (contact_pin_) contact_pin_->setup();
  if (config_.distance_enabled) distance_.setup();
  identification_ms_ = millis();
  button_candidate_ = button_pin_->digital_read();
  if (contact_pin_) contact_candidate_ = contact_pin_->digital_read();
  button_changed_ms_ = contact_changed_ms_ = millis();
#ifdef USE_OPENGARAGE_CONTROL
  pulse_outputs_.setup(door_pin_, buzzer_pin_);
  action_controller_.configure(control_config_);
  action_controller_.initialize(millis());
  control_button_.update(millis(), !button_pin_->digital_read(), action_controller_);
  ota::get_global_ota_callback()->add_global_state_listener(this);
  if (control_config_.bench_mode) {
    publish_text(family_text_, "v2.0-v2.2 pulse bench (development)");
    ESP_LOGW(TAG, "M2 BENCH ONLY: disarmed; local 3 s hold/release required to arm for 5 minutes");
  } else {
    publish_text(family_text_, hardware_v23_ ? "v2.3 pulse MVP (development)" :
                                             "v2.0-v2.2 pulse MVP (development)");
    ESP_LOGW(TAG, "Pulse MVP: verified dry-contact opener input only; no Security+");
  }
#else
  publish_text(family_text_, hardware_v23_ ? "v2.3 profile (read only)" : "v2.0-v2.2 profile (read only)");
  ESP_LOGI(TAG, "Read-only M1: door output is unclaimed");
#endif
#ifdef USE_OPENGARAGE_PULSE_MVP
  auto *base = web_server_base::global_web_server_base;
  if (base == nullptr) { action_controller_.shutdown(); mark_failed(); return; }
  // HARDWARE priority precedes web OTA's AFTER_WIFI setup, so this guard is first.
  base->add_handler(new UpdateGuard(&update_gate_));
#endif
}

void OpenGarageComponent::loop() {
  const uint32_t now = millis();
  const bool button_high = button_pin_->digital_read();
  if (button_high != button_candidate_) { button_candidate_ = button_high; button_changed_ms_ = now; }
  if (uint32_t(now - button_changed_ms_) >= 50 && (!button_valid_ || button_stable_ != button_candidate_)) {
    button_stable_ = button_candidate_;
    button_valid_ = true;
    publish_binary(button_sensor_, !button_stable_);
  }
  if (contact_pin_) {
    const bool high = contact_pin_->digital_read();
    if (high != contact_candidate_) { contact_candidate_ = high; contact_changed_ms_ = now; }
    if (uint32_t(now - contact_changed_ms_) >= 50) { contact_stable_ = contact_candidate_; contact_valid_ = true; }
  }
  if (!identification_done_ && uint32_t(now - identification_ms_) >= 50) {
    identification_ms_ = now;
    led_high_samples_ += led_pin_->digital_read();
    capability_low_samples_ += !capability_pin_->digital_read();
    if (++identification_samples_ == 7) {
      identification_done_ = true;
      led_inverted_ = led_high_samples_ > 3;
      status_led_.setup(led_pin_, led_inverted_, now);
      const bool capability = capability_low_samples_ > 3;
      publish_binary(capability_sensor_, capability);
      if (capability != hardware_v23_) ESP_LOGW(TAG, "Capability strap differs from selected hardware profile");
#ifdef USE_OPENGARAGE_CONTROL
      control_hardware_ok_ = pulse_hardware_matches(hardware_v23_, capability, control_config_.bench_mode,
          !control_config_.bench_mode || get_mac_address_pretty() == bench_mac_);
      if (!control_hardware_ok_) ESP_LOGW(TAG, "Pulse hardware family/bench identity mismatch: controls inhibited");
#endif
    }
  }
  if (config_.distance_enabled) distance_.loop(now);
#ifdef USE_OPENGARAGE_CONTROL
  service_control_(now);
#endif
  if (uint32_t(now - publish_ms_) >= 1000) { publish_ms_ = now; publish_(now); }
  status_led_.loop(now);
}

void OpenGarageComponent::publish_(uint32_t now) {
  std::optional<uint16_t> distance;
  if (config_.distance_enabled) distance = distance_.distance(now);
  const auto contact = contact_valid_ ? std::optional<bool>(contact_stable_) : std::nullopt;
  const auto state = resolve_state(config_, distance, contact);
  if (distance_sensor_) distance_sensor_->publish_state(distance ? float(*distance) : NAN);
  if (timeout_sensor_) timeout_sensor_->publish_state(float(distance_.timeout_count()));
  publish_binary(health_sensor_, config_.distance_enabled ? std::optional<bool>(!distance.has_value()) : std::nullopt);
  publish_binary(door_sensor_, state.door == DoorState::UNKNOWN ? std::nullopt : std::optional<bool>(state.door != DoorState::CLOSED));
  publish_binary(vehicle_sensor_, state.vehicle == VehicleState::PRESENT ? std::optional<bool>(true) :
      state.vehicle == VehicleState::ABSENT ? std::optional<bool>(false) : std::nullopt);
  publish_binary(contact_sensor_, contact_open(config_.contact_type, contact));
  publish_binary(button_sensor_, button_valid_ ? std::optional<bool>(!button_stable_) : std::nullopt);
  publish_text(door_text_, door_state_name(state.door));
  publish_text(vehicle_text_, vehicle_state_name(state.vehicle));
#ifdef USE_OPENGARAGE_PULSE_MVP
  const bool known = state.door == DoorState::OPEN || state.door == DoorState::CLOSED;
  publish_binary(state_valid_sensor_, known);
  if (cover_) cover_->observe(state.door);
#endif
}

void OpenGarageComponent::on_shutdown() {
#ifdef USE_OPENGARAGE_CONTROL
  action_controller_.shutdown();
#endif
  if (config_.distance_enabled) distance_.shutdown();
  status_led_.stop();
}

void OpenGarageComponent::dump_config() {
#ifdef USE_OPENGARAGE_CONTROL
  ESP_LOGCONFIG(TAG, "Pulse control: GPIO15 output, GPIO13 buzzer; bench mode %s; no Security+",
                control_config_.bench_mode ? "YES" : "NO");
  ESP_LOGCONFIG(TAG, "Warning %u ms; pulse %u ms; lockout %u ms; boot disarmed",
                control_config_.warning_ms, control_config_.pulse_ms, control_config_.lockout_ms);
#else
  ESP_LOGCONFIG(TAG, "OpenGarage M1 read-only; profile: %s; distance: %s; GPIO15 unclaimed",
                hardware_v23_ ? "v2.3" : "v2.0-v2.2", config_.distance_enabled ? "enabled" : "disabled");
#endif
  ESP_LOGCONFIG(TAG, "Status LED: %s", status_led_.enabled() ? "nominal 1 ms SDK-timed pulse every 1000 ms" : "off");
}

#ifdef USE_OPENGARAGE_CONTROL
void OpenGarageComponent::service_control_(uint32_t now) {
  // The raw button's cancel path runs BEFORE dispatch, including exactly at the deadline.
  control_button_.update(now, !button_pin_->digital_read(), action_controller_);
  const auto distance = config_.distance_enabled ? distance_.distance(now) : std::optional<uint16_t>{};
  const auto contact = contact_valid_ ? std::optional<bool>(contact_stable_) : std::nullopt;
  const auto state = resolve_state(config_, distance, contact);
  action_controller_.update(now, state.door, identification_done_ && control_hardware_ok_,
                            wifi::global_wifi_component != nullptr && wifi::global_wifi_component->is_connected());
  publish_control_();
}

void OpenGarageComponent::publish_control_() {
  // Do not republish unchanged diagnostics on every main-loop iteration.
  if (armed_sensor_ && (!armed_sensor_->has_state() || armed_sensor_->state != action_controller_.armed()))
    armed_sensor_->publish_state(action_controller_.armed());
  publish_text(phase_text_, action_phase_name(action_controller_.phase()));
  publish_text(reason_text_, action_reason_name(action_controller_.reason()));
  if (pulse_count_sensor_ && (!pulse_count_sensor_->has_state() || pulse_count_sensor_->state != action_controller_.dispatches()))
    pulse_count_sensor_->publish_state(action_controller_.dispatches());
}

void OpenGarageComponent::request_control(bool cancel) {
  const uint32_t now = millis();
  if (cancel) action_controller_.cancel(now);  // Never tick a due warning before Cancel.
  else {
    service_control_(now);  // Re-evaluate current sensor freshness before accepting a request.
    action_controller_.request_toggle(now);
  }
  publish_control_();
}

void OpenGarageComponent::on_ota_global_state(ota::OTAState, float, uint8_t, ota::OTAComponent *) {
  // An attempted/failed OTA does not re-arm; require a reboot and deliberate local arming.
  action_controller_.shutdown();
}
#endif

#ifdef USE_OPENGARAGE_PULSE_MVP
void OpenGarageComponent::request_door(DoorCommand command) {
  const uint32_t now = millis();
  service_control_(now);
  action_controller_.request(now, command);
  publish_control_();
}

void OpenGarageComponent::enter_update_mode() {
  update_gate_.enter();  // Stops outputs first; failed/aborted updates cannot reenable control.
  publish_control_();
}
#endif

}  // namespace esphome::opengarage
