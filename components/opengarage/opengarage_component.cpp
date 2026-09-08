// SPDX-License-Identifier: GPL-3.0-or-later
#include "opengarage_component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <cmath>
#include <cstdio>
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
#if (defined(USE_OPENGARAGE_SECPLUS1) || defined(USE_OPENGARAGE_SECPLUS2_SYNC)) && !defined(USE_OPENGARAGE_CONTROL)
  ota::get_global_ota_callback()->add_global_state_listener(this);
#endif
#ifdef USE_OPENGARAGE_CONTROL
#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
  pulse_outputs_.setup(buzzer_pin_);
#else
  pulse_outputs_.setup(door_pin_, buzzer_pin_);
#endif
  action_controller_.configure(control_config_);
  action_controller_.initialize(millis());
  control_button_.update(millis(), !button_pin_->digital_read(), action_controller_);
  ota::get_global_ota_callback()->add_global_state_listener(this);
#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
  publish_text(family_text_, "v2.3 Security+ 1.0 control prototype");
  ESP_LOGW(TAG, "Security+ 1.0 controls: buzzer warning; shared UART owner; no Stop or lock command");
#else
  if (control_config_.bench_mode) {
    publish_text(family_text_, "v2.0-v2.2 pulse bench (development)");
    ESP_LOGW(TAG, "M2 BENCH ONLY: disarmed; local 3 s hold/release required to arm for 5 minutes");
  } else {
    publish_text(family_text_, hardware_v23_ ? "v2.3 pulse MVP (development)" :
                                             "v2.0-v2.2 pulse MVP (development)");
    ESP_LOGW(TAG, "Pulse MVP: verified dry-contact opener input only; no Security+");
  }
#endif
#else
#ifdef USE_OPENGARAGE_SECPLUS1
  publish_text(family_text_, "v2.3 Security+ 1.0 status prototype");
  ESP_LOGW(TAG, "Security+ 1.0 status only; no door control; panel polling %s",
           secplus1_tx_pin_ ? "explicitly enabled if needed" : "disabled");
#elif defined(USE_OPENGARAGE_SECPLUS2_SYNC)
  publish_text(family_text_, "v2.3 Security+ 2.0 query prototype");
  ESP_LOGW(TAG, "Security+ 2.0 queries only; experimental zero-on-boot counter; no motion controls");
#elif defined(USE_OPENGARAGE_SECPLUS2_RX)
  publish_text(family_text_, "v2.3 Security+ 2.0 RX prototype");
  ESP_LOGW(TAG, "Security+ 2.0 passive receive prototype: no TX, queries, control or panel emulation");
#else
  publish_text(family_text_, hardware_v23_ ? "v2.3 profile (read only)" : "v2.0-v2.2 profile (read only)");
  ESP_LOGI(TAG, "Read-only M1: door output is unclaimed");
#endif
#endif
#if defined(USE_OPENGARAGE_PULSE_MVP) || defined(USE_OPENGARAGE_SECPLUS1_CONTROL)
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
#ifdef USE_OPENGARAGE_SECPLUS1
      if (hardware_v23_ && capability && !secplus1_stopped_) {
        secplus1_.start(secplus1_rx_pin_, secplus1_tx_pin_, now);
        ESP_LOGI(TAG, "Security+ 1.0 receiver started: %s", secplus1_.started() ? "YES" : "NO");
      } else ESP_LOGW(TAG, "Security+ 1.0 receiver inhibited: hardware mismatch or update in progress");
#endif
#ifdef USE_OPENGARAGE_SECPLUS2_RX
      if (hardware_v23_ && capability && secplus2_rx_pin_ != nullptr) {
#ifdef USE_OPENGARAGE_SECPLUS2_SYNC
        if (!secplus2_stopped_) secplus2_.start_queries(secplus2_rx_pin_, secplus2_tx_pin_, secplus2_client_, now);
#else
        secplus2_.start(secplus2_rx_pin_->get_pin());
#endif
      } else {
        ESP_LOGW(TAG, "Security+ RX hardware mismatch: receiver not initialized");
      }
#endif
#ifdef USE_OPENGARAGE_CONTROL
      control_hardware_ok_ = pulse_hardware_matches(hardware_v23_, capability, control_config_.bench_mode,
          !control_config_.bench_mode || get_mac_address_pretty() == bench_mac_);
      if (!control_hardware_ok_) ESP_LOGW(TAG, "Pulse hardware family/bench identity mismatch: controls inhibited");
#endif
    }
  }
#ifdef USE_OPENGARAGE_SECPLUS1
  secplus1_.loop(now);
#elif defined(USE_OPENGARAGE_SECPLUS2_RX)
  secplus2_.loop(now);  // Bounded receive pump, before sensing and entity publication.
#endif
  if (config_.distance_enabled) distance_.loop(now);
#ifdef USE_OPENGARAGE_CONTROL
  service_control_(now);
#endif
#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
  light_intent_.tick(now, light_enabled_());
  if (opener_light_) opener_light_->observe(secplus1_.receiver().light());
#endif
  if (uint32_t(now - publish_ms_) >= 1000) { publish_ms_ = now; publish_(now); }
  status_led_.loop(now);
}

void OpenGarageComponent::publish_(uint32_t now) {
  std::optional<uint16_t> distance;
  if (config_.distance_enabled) distance = distance_.distance(now);
  const auto contact = contact_valid_ ? std::optional<bool>(contact_stable_) : std::nullopt;
  DoorState protocol_state = DoorState::UNKNOWN;
#ifdef USE_OPENGARAGE_SECPLUS1
  auto &receiver = secplus1_.receiver();
  receiver.tick(now);
  protocol_state = receiver.door();
  publish_binary(secplus1_binary_[0], receiver.valid());
  publish_binary(secplus1_binary_[1], receiver.light());
  publish_binary(secplus1_binary_[2], receiver.locked());
  publish_text(secplus1_panel_text_, secplus1_panel_name(secplus1_.panel_state()));
  publish_binary(secplus1_rx_level_, secplus1_.rx_high());
  if (secplus1_trace_text_) {
    char trace[160]; receiver.format_trace(trace, sizeof(trace));
    publish_text(secplus1_trace_text_, trace[0] ? trace : "No RX bytes");
  }
  if (secplus1_block_text_) {
    char blocked[80]; secplus1_.format_tx_block(blocked, sizeof(blocked));
    publish_text(secplus1_block_text_, blocked);
  }
  if (const auto frame = receiver.last_frame()) {
    char formatted[6]; std::snprintf(formatted, sizeof(formatted), "%02X %02X", unsigned(*frame >> 8), unsigned(*frame & 0xFF));
    publish_text(secplus1_frame_text_, formatted);
  }
  const auto &stats = receiver.stats();
  const uint32_t values[] = {stats.bytes, stats.frames, stats.door_frames, stats.light_lock_frames, stats.parity_errors,
      stats.partial_timeouts, stats.overflows, stats.ignored_bytes, stats.invalid_frames, stats.panel37,
      stats.tx_bytes, stats.tx_deferred, stats.tx_errors, stats.max_service_us, stats.high_water,
      stats.deferred_backlog, stats.deferred_partial, stats.deferred_byte, stats.deferred_isr, stats.deferred_high};
  static_assert(sizeof(values) / sizeof(values[0]) == 20, "Security+ 1.0 diagnostic index mapping");
  for (size_t i = 0; i < secplus1_diagnostics_.size(); ++i) {
    auto *sensor = secplus1_diagnostics_[i];
    if (sensor && (!sensor->has_state() || sensor->state != values[i])) sensor->publish_state(values[i]);
  }
#elif defined(USE_OPENGARAGE_SECPLUS2_RX)
  auto &receiver = secplus2_.receiver();
  receiver.tick(now);
  protocol_state = receiver.door();
  publish_binary(secplus2_binary_[0], receiver.valid());
  publish_binary(secplus2_binary_[1], receiver.light());
  publish_binary(secplus2_binary_[2], receiver.locked());
  const auto &stats = receiver.stats();
  const uint32_t values[] = {stats.bytes, stats.frames, stats.status_frames, stats.decode_errors,
      stats.partial_timeouts, stats.overflows, stats.unknown_commands, stats.semantic_errors,
      stats.high_water, stats.max_service_us};
  for (size_t i = 0; i < secplus2_diagnostics_.size(); ++i) {
    auto *sensor = secplus2_diagnostics_[i];
    if (sensor && (!sensor->has_state() || sensor->state != values[i])) sensor->publish_state(values[i]);
  }
#endif
  const auto state = resolve_state(config_, distance, contact, protocol_state);
#ifdef USE_OPENGARAGE_SECPLUS2_SYNC
  const auto &session = secplus2_.session();
  publish_text(secplus2_sync_text_, session.state_name());
  char counter[11]; std::snprintf(counter, sizeof(counter), "%lu", static_cast<unsigned long>(session.rolling()));
  publish_text(secplus2_rolling_text_, counter); // Exact uint32 text, never float-authoritative storage.
  if (secplus2_openings_) {
    const auto openings = secplus2_.receiver().openings();
    const float value = openings ? float(*openings) : NAN;
    if (!secplus2_openings_->has_state() || (std::isnan(secplus2_openings_->state) != std::isnan(value)) ||
        (!std::isnan(value) && secplus2_openings_->state != value)) secplus2_openings_->publish_state(value);
  }
  const uint32_t tx_values[] = {secplus2_.query_writes(), secplus2_.collisions(), secplus2_.deferrals(),
                              secplus2_.tx_errors(), secplus2_.max_tx_us()};
  for (size_t i = 0; i < secplus2_tx_diagnostics_.size(); ++i) {
    auto *s = secplus2_tx_diagnostics_[i];
    if (s && (!s->has_state() || s->state != tx_values[i])) s->publish_state(tx_values[i]);
  }
#endif
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
#if defined(USE_OPENGARAGE_PULSE_MVP) || defined(USE_OPENGARAGE_SECPLUS1_CONTROL)
#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
  const bool known = state.door != DoorState::UNKNOWN;
#else
  const bool known = state.door == DoorState::OPEN || state.door == DoorState::CLOSED;
#endif
  publish_binary(state_valid_sensor_, known);
  if (cover_) cover_->observe(state.door);
#endif
}

void OpenGarageComponent::on_shutdown() {
#ifdef USE_OPENGARAGE_SECPLUS1
  secplus1_stopped_ = true;
  secplus1_.stop();
#endif
#ifdef USE_OPENGARAGE_SECPLUS2_RX
#ifdef USE_OPENGARAGE_SECPLUS2_SYNC
  secplus2_stopped_ = true;
#endif
  secplus2_.stop();
#endif
#ifdef USE_OPENGARAGE_CONTROL
  action_controller_.shutdown();
#endif
#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
  light_intent_.cancel();
#endif
  if (config_.distance_enabled) distance_.shutdown();
  status_led_.stop();
}

void OpenGarageComponent::dump_config() {
#ifdef USE_OPENGARAGE_CONTROL
#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
  ESP_LOGCONFIG(TAG, "Security+ 1.0 control: shared GPIO5/15 UART; GPIO13 warning; no dry-contact output");
#else
  ESP_LOGCONFIG(TAG, "Pulse control: GPIO15 output, GPIO13 buzzer; bench mode %s; no Security+",
                control_config_.bench_mode ? "YES" : "NO");
#endif
  ESP_LOGCONFIG(TAG, "Warning %u ms; pulse %u ms; lockout %u ms; boot disarmed",
                control_config_.warning_ms, control_config_.pulse_ms, control_config_.lockout_ms);
#else
#ifdef USE_OPENGARAGE_SECPLUS1
  ESP_LOGCONFIG(TAG, "Security+ 1.0: inverted 1200 8E1, GPIO5 RX; GPIO15 %s; GPIO13 unclaimed",
                secplus1_tx_pin_ ? "panel polling only" : "unclaimed");
#elif defined(USE_OPENGARAGE_SECPLUS2_RX)
  ESP_LOGCONFIG(TAG, "Security+ 2.0 RX: GPIO5, inverted 9600 8N1; GPIO15/GPIO13 unclaimed");
  ESP_LOGCONFIG(TAG, "RX started: %s; status valid: %s; 128-byte / 1280-edge buffers",
                secplus2_.started() ? "YES" : "NO", secplus2_.receiver().valid() ? "YES" : "NO");
#else
  ESP_LOGCONFIG(TAG, "OpenGarage M1 read-only; profile: %s; distance: %s; GPIO15 unclaimed",
                hardware_v23_ ? "v2.3" : "v2.0-v2.2", config_.distance_enabled ? "enabled" : "disabled");
#endif
#endif
  ESP_LOGCONFIG(TAG, "Status LED: %s", status_led_.enabled() ? "nominal 1 ms SDK-timed pulse every 1000 ms" : "off");
}

#ifdef USE_OPENGARAGE_CONTROL
void OpenGarageComponent::service_control_(uint32_t now) {
  // The raw button's cancel path runs BEFORE dispatch, including exactly at the deadline.
  control_button_.update(now, !button_pin_->digital_read(), action_controller_);
  const auto distance = config_.distance_enabled ? distance_.distance(now) : std::optional<uint16_t>{};
  const auto contact = contact_valid_ ? std::optional<bool>(contact_stable_) : std::nullopt;
  DoorState protocol = DoorState::UNKNOWN;
  bool hardware_ok = identification_done_ && control_hardware_ok_;
#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
  secplus1_.receiver().tick(now);
  protocol = secplus1_.receiver().door();
  hardware_ok = hardware_ok && secplus1_.controls_available();
#endif
  const auto state = resolve_state(config_, distance, contact, protocol);
  action_controller_.update(now, state.door, hardware_ok,
                            wifi::global_wifi_component != nullptr && wifi::global_wifi_component->is_connected());
  publish_control_();
}

void OpenGarageComponent::publish_control_() {
  // Do not republish unchanged diagnostics on every main-loop iteration.
  if (armed_sensor_ && (!armed_sensor_->has_state() || armed_sensor_->state != action_controller_.armed()))
    armed_sensor_->publish_state(action_controller_.armed());
#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
  publish_text(phase_text_, action_controller_.phase() == ActionPhase::PULSING ? "Releasing protocol button" :
      action_phase_name(action_controller_.phase()));
  publish_text(reason_text_, action_controller_.reason() == ActionReason::DISPATCHED ? "Door command sent" :
      action_reason_name(action_controller_.reason()));
#else
  publish_text(phase_text_, action_phase_name(action_controller_.phase()));
  publish_text(reason_text_, action_reason_name(action_controller_.reason()));
#endif
  if (pulse_count_sensor_ && (!pulse_count_sensor_->has_state() || pulse_count_sensor_->state != action_controller_.dispatches()))
    pulse_count_sensor_->publish_state(action_controller_.dispatches());
#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
  publish_text(light_reason_, light_intent_.reason());
  if (light_count_ && (!light_count_->has_state() || light_count_->state != secplus1_.light_commands()))
    light_count_->publish_state(secplus1_.light_commands());
#endif
}

void OpenGarageComponent::request_control(bool cancel) {
  const uint32_t now = millis();
  if (cancel) {
    action_controller_.cancel(now);  // Never tick a due warning before Cancel.
#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
    light_intent_.cancel();
#endif
  }
  else {
    service_control_(now);  // Re-evaluate current sensor freshness before accepting a request.
    action_controller_.request_toggle(now);
  }
  publish_control_();
}

void OpenGarageComponent::on_ota_global_state(ota::OTAState, float, uint8_t, ota::OTAComponent *) {
  // An attempted/failed OTA does not re-arm; require a reboot and deliberate local arming.
  action_controller_.shutdown();
#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
  light_intent_.cancel();
  secplus1_stopped_ = true;
  secplus1_.stop();
#endif
}
#endif

#ifdef USE_OPENGARAGE_SECPLUS2_SYNC
void OpenGarageComponent::on_ota_global_state(ota::OTAState, float, uint8_t, ota::OTAComponent *) {
  secplus2_stopped_ = true;
  secplus2_.stop(); // Includes OTA before identification, failure, and completed uploads.
}
#endif

#if defined(USE_OPENGARAGE_SECPLUS1) && !defined(USE_OPENGARAGE_CONTROL)
void OpenGarageComponent::on_ota_global_state(ota::OTAState, float, uint8_t, ota::OTAComponent *) {
  // Even failed OTA leaves polling stopped until reboot. No mode changes or replay.
  secplus1_stopped_ = true;
  secplus1_.stop();
}
#endif

#if defined(USE_OPENGARAGE_PULSE_MVP) || defined(USE_OPENGARAGE_SECPLUS1_CONTROL)
void OpenGarageComponent::request_door(DoorCommand command) {
  const uint32_t now = millis();
  service_control_(now);
  action_controller_.request(now, command);
  publish_control_();
}

void OpenGarageComponent::enter_update_mode() {
#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
  action_controller_.shutdown();
  light_intent_.cancel();
  secplus1_stopped_ = true;
  secplus1_.stop();
#endif
  update_gate_.enter();  // Release/stop first, then open the browser gate; latched until reboot.
  publish_control_();
}
#endif

#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
bool OpenGarageComponent::light_enabled_() const {
  return identification_done_ && control_hardware_ok_ && !secplus1_stopped_ &&
      !update_gate_.open() && action_controller_.armed() && !action_controller_.pending() &&
      wifi::global_wifi_component != nullptr && wifi::global_wifi_component->is_connected();
}
void OpenGarageComponent::request_light(bool target) {
  // Do not advance a due door warning from a competing light callback.
  light_intent_.request(millis(), target, light_enabled_());
  publish_control_();
}
#endif

}  // namespace esphome::opengarage
