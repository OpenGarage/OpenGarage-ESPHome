// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/defines.h"
#ifdef USE_OPENGARAGE_SECPLUS2_CONTROL
#include "pulse_cover.h"
#include "opener_light_commands.h"
#include "secplus2_transport.h"
#include "esphome/components/light/light_output.h"

namespace esphome::opengarage {
class Secplus2Cover : public PulseCover {
 public:
  explicit Secplus2Cover(CoverCommands *parent) : PulseCover(parent) {}
  void observe(DoorState state) {
    // Nothing observed yet to retain. Native API subscription can still expose
    // Cover's default Open position; this guard does not provide HA unknown state.
    if (!published_ && state == DoorState::UNKNOWN) return;
    const auto operation = state == DoorState::OPENING ? cover::COVER_OPERATION_OPENING :
        state == DoorState::CLOSING ? cover::COVER_OPERATION_CLOSING : cover::COVER_OPERATION_IDLE;
    const float next = state == DoorState::CLOSED ? cover::COVER_CLOSED :
        state == DoorState::OPEN ? cover::COVER_OPEN : state == DoorState::UNKNOWN ? position : 0.5f;
    // 0.5 only represents a non-endpoint, not measured travel percentage.
    // No positional command, guessed Stop, optimistic motion, or state restoration.
    if (!published_ || next != position || operation != current_operation) {
      published_ = true; position = next; current_operation = operation; publish_state(false);
    }
  }
};

// One bounded ON/OFF light intent, not an action/retry queue. Revalidate the
// reported baseline before writing; do not retry an unacknowledged command.
template<class Port> class TargetLightIntent {
 public:
  explicit TargetLightIntent(Port &port) : port_(port) {}
  bool request(uint32_t now, bool target, bool enabled) {
    port_.receiver().tick(now);
    if (!enabled || !port_.controls_available()) return reject_("Controls unavailable");
    // A door/light/lock release may finish inside the bounded bus-idle wait;
    // never interleave a new press with that tail or retry a transmitted command.
    if (waiting_ || (cooldown_ && uint32_t(now - sent_ms_) < 2000))
      return reject_("Busy; not queued");
    const auto value = port_.receiver().light();
    if (!value) return reject_("Light state unknown");
    if (*value == target) return reject_("Already at requested state; no command");
    target_ = target; requested_ms_ = now; waiting_ = true;
    reason_ = "Waiting for bus idle";
    return true;
  }
  void tick(uint32_t now, bool enabled) {
    port_.receiver().tick(now);
    if (!waiting_) return;
    if (!enabled || !port_.controls_available()) { cancel(); return; }
    const auto value = port_.receiver().light();
    if (!value) { waiting_ = false; reason_ = "Light state unknown; canceled"; return; }
    if (*value == target_) { waiting_ = false; reason_ = "Target observed; no command"; return; }
    if (uint32_t(now - requested_ms_) >= 1000) { waiting_ = false; reason_ = "Bus wait expired; no command"; return; }
    if (!port_.command_idle(now)) return;
    waiting_ = false;
    if (port_.set_light(now, target_)) {
      sent_ms_ = now; cooldown_ = true; reason_ = "Light command sent; awaiting reported state";
    } else reason_ = "Light write refused or failed; not retried";
  }
  void cancel() { waiting_ = false; reason_ = "Canceled; no pending light command"; }
  bool waiting() const { return waiting_; }
  const char *reason() const { return reason_; }
 protected:
  bool reject_(const char *reason) { reason_ = reason; return false; }
  Port &port_;
  uint32_t requested_ms_{0}, sent_ms_{0};
  bool waiting_{false}, target_{false}, cooldown_{false};
  const char *reason_{"No light command"};
};

using Secplus2LightIntent = TargetLightIntent<Secplus2Transport>;
class Secplus2Light : public light::LightOutput {
 public:
  explicit Secplus2Light(OpenerLightCommands *parent) : parent_(parent) {}
  light::LightTraits get_traits() override {
    light::LightTraits traits;
    traits.set_supported_color_modes({light::ColorMode::ON_OFF});
    return traits;
  }
  void setup_state(light::LightState *state) override { state_ = state; }
  void update_state(light::LightState *state) override {
    if (state->is_transformer_active()) { restore_observation_(); return; }
    // ESPHome's initial ALWAYS_OFF call is bookkeeping, never an opener command.
    if (initial_) initial_ = false;
    else parent_->request_light(state->current_values.is_on());
    restore_observation_();
  }
  void write_state(light::LightState *) override {}  // Never replay a deferred LightState write.
  void observe(std::optional<bool> value) {
    if (!value || !state_) return;  // Native light lacks missing-state; companion binary is authoritative.
    const bool changed = !observed_ || *observed_ != *value || state_->remote_values.is_on() != *value;
    observed_ = value;
    restore_observation_();
    if (changed) state_->publish_state();
  }
 protected:
  void restore_observation_() {
    if (!state_) return;
    // Do not publish a requested target as acknowledged state. Before the first
    // known report the native light has an OFF placeholder; consult Light State.
    state_->current_values.set_state(observed_.value_or(false));
    state_->remote_values.set_state(observed_.value_or(false));
  }
  OpenerLightCommands *parent_;
  light::LightState *state_{nullptr};
  std::optional<bool> observed_;
  bool initial_{true};
};
}  // namespace esphome::opengarage
#endif
