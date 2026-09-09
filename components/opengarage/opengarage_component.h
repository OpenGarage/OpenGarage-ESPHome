// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "distance_sensor.h"
#include "state_resolver.h"
#include "status_led.h"
#ifdef USE_OPENGARAGE_SECPLUS2_RX
#include "secplus2_transport.h"
#endif
#ifdef USE_OPENGARAGE_SECPLUS1
#include "secplus1_transport.h"
#endif
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#ifdef USE_OPENGARAGE_THRESHOLDS
#include "threshold_number.h"
#include "esphome/core/preferences.h"
#endif
#ifdef USE_OPENGARAGE_CONTROL
#include "update_status.h"
#ifdef USE_OPENGARAGE_UNIFIED
#include "unified_port.h"
#include "protocol_select.h"
#include "remote_lock.h"
#include "esphome/core/preferences.h"
#elif defined(USE_OPENGARAGE_SECPLUS1_CONTROL)
#include "secplus1_outputs.h"
#include "secplus1_controls.h"
#include "remote_lock.h"
#elif defined(USE_OPENGARAGE_SECPLUS2_CONTROL)
#include "secplus2_outputs.h"
#include "secplus2_controls.h"
#include "remote_lock.h"
#else
#include "pulse_outputs.h"
#endif
#include "esphome/components/button/button.h"
#endif
#if defined(USE_OPENGARAGE_CONTROL) || defined(USE_OPENGARAGE_SECPLUS1) || defined(USE_OPENGARAGE_SECPLUS2_SYNC)
#include "esphome/components/ota/ota_backend.h"
#endif
#if defined(USE_OPENGARAGE_PULSE_MVP) || defined(USE_OPENGARAGE_SECPLUS1_CONTROL) || defined(USE_OPENGARAGE_SECPLUS2_CONTROL)
#include "pulse_cover.h"
#include "update_guard.h"
#endif

namespace esphome::opengarage {

class OpenGarageComponent : public Component
#if defined(USE_OPENGARAGE_CONTROL) || defined(USE_OPENGARAGE_SECPLUS1) || defined(USE_OPENGARAGE_SECPLUS2_SYNC)
    , public ota::OTAGlobalStateListener
#endif
#if defined(USE_OPENGARAGE_PULSE_MVP) || defined(USE_OPENGARAGE_SECPLUS1_CONTROL) || defined(USE_OPENGARAGE_SECPLUS2_CONTROL)
    , public CoverCommands
#endif
#if defined(USE_OPENGARAGE_SECPLUS1_CONTROL) || defined(USE_OPENGARAGE_SECPLUS2_CONTROL)
    , public OpenerLightCommands
    , public RemoteLockCommands
#endif
#ifdef USE_OPENGARAGE_UNIFIED
    , public ProtocolSettingCommands
#endif
#ifdef USE_OPENGARAGE_THRESHOLDS
    , public ThresholdCommands
#endif
{
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_shutdown() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }
  void set_hardware_v23(bool value) { hardware_v23_ = value; }
  void set_source(StateSource value) { config_.source = value; }
  void set_mounting(Mounting value) { config_.mounting = value; }
  void set_contact_type(ContactType value) { config_.contact_type = value; }
  void set_distance_enabled(bool value) { config_.distance_enabled = value; }
  void set_door_threshold(uint16_t value) { config_.door_threshold_cm = value; }
  void set_vehicle_threshold(uint16_t value) { config_.vehicle_threshold_cm = value; }
#ifdef USE_OPENGARAGE_THRESHOLDS
  void load_threshold_settings();
  void set_threshold_number(bool door, ThresholdNumber *value) {
    (door ? door_threshold_number_ : vehicle_threshold_number_) = value;
  }
  void request_threshold(bool door, float value) override;
#endif
  void set_distance_pins(InternalGPIOPin *trigger, InternalGPIOPin *echo) { distance_.set_pins(trigger, echo); }
  void set_distance_config(FilterMode mode, TimeoutPolicy policy, uint16_t margin, uint32_t stale, uint32_t interval) {
    distance_.configure(mode, policy, margin, stale, interval);
  }
  void set_led_pin(InternalGPIOPin *pin) { led_pin_ = pin; }
  void set_status_led_enabled(bool value) { status_led_.set_enabled(value); }
  void set_contact_pin(InternalGPIOPin *pin) { contact_pin_ = pin; }
  void set_button_pin(InternalGPIOPin *pin) { button_pin_ = pin; }
  void set_capability_pin(InternalGPIOPin *pin) { capability_pin_ = pin; }
  void set_distance_sensor(sensor::Sensor *value) { distance_sensor_ = value; }
  void set_timeout_sensor(sensor::Sensor *value) { timeout_sensor_ = value; }
  void set_door_sensor(binary_sensor::BinarySensor *value) { door_sensor_ = value; }
  void set_vehicle_sensor(binary_sensor::BinarySensor *value) { vehicle_sensor_ = value; }
  void set_contact_sensor(binary_sensor::BinarySensor *value) { contact_sensor_ = value; }
  void set_button_sensor(binary_sensor::BinarySensor *value) { button_sensor_ = value; }
  void set_health_sensor(binary_sensor::BinarySensor *value) { health_sensor_ = value; }
  void set_capability_sensor(binary_sensor::BinarySensor *value) { capability_sensor_ = value; }
  void set_door_text(text_sensor::TextSensor *value) { door_text_ = value; }
  void set_vehicle_text(text_sensor::TextSensor *value) { vehicle_text_ = value; }
  void set_family_text(text_sensor::TextSensor *value) { family_text_ = value; }
#ifdef USE_OPENGARAGE_UNIFIED
  // Codegen calls this before entity configuration and App.setup(), after the
  // platform preference store exists. No I/O or control is started here.
  void load_unified_settings(OpenerProtocol protocol, PanelEmulation panel);
  void set_unified_pins(InternalGPIOPin *rx, InternalGPIOPin *tx) {
    secplus1_rx_pin_ = secplus2_rx_pin_ = rx;
    secplus1_tx_pin_ = secplus2_tx_pin_ = door_pin_ = tx;
  }
  void set_unified_client(uint32_t value) { secplus2_client_ = value; }
  void set_protocol_select(ProtocolSelect *value) { protocol_select_ = value; }
  void set_panel_select(ProtocolSelect *value) { panel_select_ = value; }
  void set_configuration_text(text_sensor::TextSensor *value) { configuration_text_ = value; }
  void request_setting(bool panel, size_t index) override;
  uint32_t mode_entity_fields(uint32_t fields, uint8_t mask) const {
    return fields | (protocol_exposes(active_settings_.protocol, mask) ? 0U : (1U << 24));
  }
#endif
#ifdef USE_OPENGARAGE_SECPLUS1
  void set_secplus1_rx_pin(InternalGPIOPin *pin) { secplus1_rx_pin_ = pin; }
  void set_secplus1_tx_pin(InternalGPIOPin *pin) { secplus1_tx_pin_ = pin; }
  void set_secplus1_timeout(uint32_t ms) { secplus1_.receiver().set_status_timeout(ms); }
  void set_secplus1_panel_text(text_sensor::TextSensor *value) { secplus1_panel_text_ = value; }
  void set_secplus1_frame_text(text_sensor::TextSensor *value) { secplus1_frame_text_ = value; }
  void set_secplus1_trace_text(text_sensor::TextSensor *value) { secplus1_trace_text_ = value; }
  void set_secplus1_block_text(text_sensor::TextSensor *value) { secplus1_block_text_ = value; }
  void set_secplus1_rx_level(binary_sensor::BinarySensor *value) { secplus1_rx_level_ = value; }
  void set_secplus1_binary(size_t index, binary_sensor::BinarySensor *value) {
    if (index < secplus1_binary_.size()) secplus1_binary_[index] = value;
  }
  void set_secplus1_diagnostic(size_t index, sensor::Sensor *value) {
    if (index < secplus1_diagnostics_.size()) secplus1_diagnostics_[index] = value;
  }
#endif
#ifdef USE_OPENGARAGE_SECPLUS2_RX
  void set_secplus2_rx_pin(InternalGPIOPin *pin) { secplus2_rx_pin_ = pin; }
  void set_secplus2_timeout(uint32_t ms) { secplus2_.receiver().set_status_timeout(ms); }
  void set_secplus2_binary(size_t index, binary_sensor::BinarySensor *value) {
    if (index < secplus2_binary_.size()) secplus2_binary_[index] = value;
  }
  void set_secplus2_diagnostic(size_t index, sensor::Sensor *value) {
    if (index < secplus2_diagnostics_.size()) secplus2_diagnostics_[index] = value;
  }
#endif
#ifdef USE_OPENGARAGE_SECPLUS2_SYNC
  void set_secplus2_query_config(InternalGPIOPin *tx, uint32_t client) { secplus2_tx_pin_ = tx; secplus2_client_ = client; }
  void set_secplus2_sync_text(text_sensor::TextSensor *value) { secplus2_sync_text_ = value; }
  void set_secplus2_rolling_text(text_sensor::TextSensor *value) { secplus2_rolling_text_ = value; }
  void set_secplus2_openings(sensor::Sensor *value) { secplus2_openings_ = value; }
  void set_secplus2_tx_diagnostic(size_t i, sensor::Sensor *value) {
    if (i < secplus2_tx_diagnostics_.size()) secplus2_tx_diagnostics_[i] = value;
  }
#endif
#ifdef USE_OPENGARAGE_CONTROL
  void set_bench_mac(const std::string &value) { bench_mac_ = value; }
  void set_control_pins(InternalGPIOPin *door, InternalGPIOPin *buzzer) { door_pin_ = door; buzzer_pin_ = buzzer; }
  void set_control_timing(uint32_t warning, uint32_t pulse, uint32_t lockout) {
    control_config_.warning_ms = warning; control_config_.pulse_ms = pulse; control_config_.lockout_ms = lockout;
  }
  void set_bench_mode(bool value) { control_config_.bench_mode = value; }
  void set_armed_sensor(binary_sensor::BinarySensor *sensor) { armed_sensor_ = sensor; }
  void set_phase_text(text_sensor::TextSensor *sensor) { phase_text_ = sensor; }
  void set_reason_text(text_sensor::TextSensor *sensor) { reason_text_ = sensor; }
  void set_pulse_count_sensor(sensor::Sensor *sensor) { pulse_count_sensor_ = sensor; }
  void request_control(bool cancel);
#endif
#if defined(USE_OPENGARAGE_CONTROL) || defined(USE_OPENGARAGE_SECPLUS1) || defined(USE_OPENGARAGE_SECPLUS2_SYNC)
  void on_ota_global_state(ota::OTAState state, float progress, uint8_t error, ota::OTAComponent *component) override;
#endif
#if defined(USE_OPENGARAGE_PULSE_MVP) || defined(USE_OPENGARAGE_SECPLUS1_CONTROL) || defined(USE_OPENGARAGE_SECPLUS2_CONTROL)
#ifdef USE_OPENGARAGE_UNIFIED
  void set_cover(UnifiedCover *value) { cover_ = value; }
  void set_light(UnifiedLight *value) { opener_light_ = value; }
#elif defined(USE_OPENGARAGE_SECPLUS1_CONTROL)
  void set_cover(Secplus1Cover *value) { cover_ = value; }
  void set_light(Secplus1Light *value) { opener_light_ = value; }
#elif defined(USE_OPENGARAGE_SECPLUS2_CONTROL)
  void set_cover(Secplus2Cover *value) { cover_ = value; }
  void set_light(Secplus2Light *value) { opener_light_ = value; }
#else
  void set_cover(PulseCover *value) { cover_ = value; }
#endif
#if defined(USE_OPENGARAGE_SECPLUS1_CONTROL) || defined(USE_OPENGARAGE_SECPLUS2_CONTROL)
  void set_light_reason(text_sensor::TextSensor *value) { light_reason_ = value; }
  void set_light_count(sensor::Sensor *value) { light_count_ = value; }
  void request_light(bool target) override;
  void set_remote_lock(RemoteLock *value) { remote_lock_ = value; }
  void set_lock_reason(text_sensor::TextSensor *value) { lock_reason_ = value; }
  void set_lock_count(sensor::Sensor *value) { lock_count_ = value; }
  void request_lock(bool target) override;
#endif
  void set_state_valid_sensor(binary_sensor::BinarySensor *value) { state_valid_sensor_ = value; }
  void request_door(DoorCommand command) override;
  void reject_cover_command() override { action_controller_.reject_unsupported(); publish_control_(); }
  void enter_update_mode();
#endif

 protected:
  void publish_(uint32_t now);
#ifdef USE_OPENGARAGE_THRESHOLDS
  bool threshold_editable_(bool door) const;
  void publish_thresholds_();
  ESPPreferenceObject threshold_preference_;
  ThresholdNumber *door_threshold_number_{nullptr}, *vehicle_threshold_number_{nullptr};
  bool thresholds_loaded_{false}, thresholds_ready_{false}, thresholds_stopped_{false};
#endif
#ifdef USE_OPENGARAGE_UNIFIED
  void stop_unified_();
  void publish_settings_();
  ESPPreferenceObject mode_preference_;
  ProtocolSettings active_settings_, pending_settings_;
  ProtocolSelect *protocol_select_{nullptr}, *panel_select_{nullptr};
  text_sensor::TextSensor *configuration_text_{nullptr};
  bool settings_loaded_{false}, configuration_stopped_{false}, settings_error_{false}, unified_ota_latched_{false};
  bool secplus1_selected_() const { return active_settings_.protocol == OpenerProtocol::SECPLUS1; }
  bool secplus2_selected_() const { return active_settings_.protocol == OpenerProtocol::SECPLUS2; }
#else
  bool secplus1_selected_() const { return true; }
  bool secplus2_selected_() const { return true; }
#endif
  ResolverConfig config_;
  DistanceSensor distance_;
  StatusLed status_led_;
  InternalGPIOPin *led_pin_{nullptr}, *contact_pin_{nullptr}, *button_pin_{nullptr}, *capability_pin_{nullptr};
  sensor::Sensor *distance_sensor_{nullptr}, *timeout_sensor_{nullptr};
  binary_sensor::BinarySensor *door_sensor_{nullptr}, *vehicle_sensor_{nullptr}, *contact_sensor_{nullptr};
  binary_sensor::BinarySensor *button_sensor_{nullptr}, *health_sensor_{nullptr}, *capability_sensor_{nullptr};
  text_sensor::TextSensor *door_text_{nullptr}, *vehicle_text_{nullptr}, *family_text_{nullptr};
  bool hardware_v23_{false}, led_inverted_{true}, identification_done_{false};
  uint8_t identification_samples_{0}, led_high_samples_{0}, capability_low_samples_{0};
  uint32_t identification_ms_{0}, publish_ms_{0};
  bool button_candidate_{false}, button_stable_{false}, button_valid_{false};
  bool contact_candidate_{false}, contact_stable_{false}, contact_valid_{false};
  uint32_t button_changed_ms_{0}, contact_changed_ms_{0};
#ifdef USE_OPENGARAGE_SECPLUS1
  Secplus1Transport secplus1_;
  InternalGPIOPin *secplus1_rx_pin_{nullptr}, *secplus1_tx_pin_{nullptr};
  std::array<binary_sensor::BinarySensor *, 4> secplus1_binary_{};
  std::array<sensor::Sensor *, 20> secplus1_diagnostics_{};
  text_sensor::TextSensor *secplus1_panel_text_{nullptr}, *secplus1_frame_text_{nullptr};
  text_sensor::TextSensor *secplus1_trace_text_{nullptr}, *secplus1_block_text_{nullptr};
  binary_sensor::BinarySensor *secplus1_rx_level_{nullptr};
  bool secplus1_stopped_{false};
#endif
#ifdef USE_OPENGARAGE_SECPLUS2_RX
  Secplus2Transport secplus2_;
  InternalGPIOPin *secplus2_rx_pin_{nullptr};
  std::array<binary_sensor::BinarySensor *, 4> secplus2_binary_{};
  std::array<sensor::Sensor *, 10> secplus2_diagnostics_{};
#endif
#ifdef USE_OPENGARAGE_SECPLUS2_SYNC
  InternalGPIOPin *secplus2_tx_pin_{nullptr};
  uint32_t secplus2_client_{0};
  text_sensor::TextSensor *secplus2_sync_text_{nullptr}, *secplus2_rolling_text_{nullptr};
  sensor::Sensor *secplus2_openings_{nullptr};
  std::array<sensor::Sensor *, 5> secplus2_tx_diagnostics_{};
  bool secplus2_stopped_{false};
#endif
#ifdef USE_OPENGARAGE_CONTROL
  void service_control_(uint32_t now);
  void publish_control_();
#ifdef USE_OPENGARAGE_UNIFIED
  UnifiedPort unified_port_{secplus1_, secplus2_};
  UnifiedOutputs pulse_outputs_{secplus1_, secplus2_};
  UnifiedLightIntent light_intent_{unified_port_};
  RemoteLockIntent<UnifiedPort> lock_intent_{unified_port_};
  UnifiedLight *opener_light_{nullptr};
#elif defined(USE_OPENGARAGE_SECPLUS1_CONTROL)
  Secplus1Outputs pulse_outputs_{secplus1_};
  Secplus1LightIntent light_intent_{secplus1_};
  RemoteLockIntent<Secplus1Transport> lock_intent_{secplus1_};
  Secplus1Light *opener_light_{nullptr};
#elif defined(USE_OPENGARAGE_SECPLUS2_CONTROL)
  Secplus2Outputs pulse_outputs_{secplus2_};
  Secplus2LightIntent light_intent_{secplus2_};
  RemoteLockIntent<Secplus2Transport> lock_intent_{secplus2_};
  Secplus2Light *opener_light_{nullptr};
#else
  PulseOutputs pulse_outputs_;
#endif
#if defined(USE_OPENGARAGE_SECPLUS1_CONTROL) || defined(USE_OPENGARAGE_SECPLUS2_CONTROL)
  text_sensor::TextSensor *light_reason_{nullptr};
  sensor::Sensor *light_count_{nullptr};
  RemoteLock *remote_lock_{nullptr};
  text_sensor::TextSensor *lock_reason_{nullptr};
  sensor::Sensor *lock_count_{nullptr};
  bool auxiliary_enabled_() const;
  bool light_enabled_() const;
  bool lock_enabled_() const;
#endif
  ActionController action_controller_{pulse_outputs_};
  UpdateStatus update_status_;
  ControlButton control_button_;
  ActionConfig control_config_;
  InternalGPIOPin *door_pin_{nullptr}, *buzzer_pin_{nullptr};
  std::string bench_mac_;
  bool control_hardware_ok_{false};
  binary_sensor::BinarySensor *armed_sensor_{nullptr};
  text_sensor::TextSensor *phase_text_{nullptr}, *reason_text_{nullptr};
  sensor::Sensor *pulse_count_sensor_{nullptr};
#endif
#if defined(USE_OPENGARAGE_PULSE_MVP) || defined(USE_OPENGARAGE_SECPLUS1_CONTROL) || defined(USE_OPENGARAGE_SECPLUS2_CONTROL)
#ifdef USE_OPENGARAGE_UNIFIED
  UnifiedCover *cover_{nullptr};
#elif defined(USE_OPENGARAGE_SECPLUS1_CONTROL)
  Secplus1Cover *cover_{nullptr};
#elif defined(USE_OPENGARAGE_SECPLUS2_CONTROL)
  Secplus2Cover *cover_{nullptr};
#else
  PulseCover *cover_{nullptr};
#endif
  binary_sensor::BinarySensor *state_valid_sensor_{nullptr};
  UpdateGate update_gate_{action_controller_};
#endif
};

#ifdef USE_OPENGARAGE_CONTROL
class ControlCommandButton : public button::Button {
 public:
  ControlCommandButton(OpenGarageComponent *parent, bool cancel) : parent_(parent), cancel_(cancel) {}
 protected:
  void press_action() override { parent_->request_control(cancel_); }
  OpenGarageComponent *parent_;
  bool cancel_;
};
#endif

#if defined(USE_OPENGARAGE_PULSE_MVP) || defined(USE_OPENGARAGE_SECPLUS1_CONTROL) || defined(USE_OPENGARAGE_SECPLUS2_CONTROL)
class UpdateModeButton : public button::Button {
 public:
  explicit UpdateModeButton(OpenGarageComponent *parent) : parent_(parent) {}
 protected:
  void press_action() override { parent_->enter_update_mode(); }
  OpenGarageComponent *parent_;
};
#endif

}  // namespace esphome::opengarage
