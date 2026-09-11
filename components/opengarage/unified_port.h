// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/defines.h"
#ifdef USE_OPENGARAGE_UNIFIED
#include "protocol_mode.h"
#include "ip_reporter.h"
#include "startup_tune.h"
#include "pulse_outputs.h"
#include "secplus1_outputs.h"
#include "secplus2_outputs.h"
#include "secplus1_controls.h"
#include "secplus2_controls.h"

namespace esphome::opengarage {
// Selection is immutable after bind(). No hot switching, no inactive UART begin,
// and no inactive backend gets a loop, command, or stop call touching shared pins.
class UnifiedPort {
 public:
  UnifiedPort(Secplus1Transport &one, Secplus2Transport &two) : one_(one), two_(two) {}
  void bind(OpenerProtocol mode) { if (!bound_) { mode_ = mode; bound_ = true; } }
  OpenerProtocol mode() const { return mode_; }
  bool security() const { return security_protocol(mode_); }
  UnifiedPort &receiver() { return *this; }
  void tick(uint32_t now) {
    if (mode_ == OpenerProtocol::SECPLUS1) one_.receiver().tick(now);
    if (mode_ == OpenerProtocol::SECPLUS2) two_.receiver().tick(now);
  }
  DoorState door() const {
    if (mode_ == OpenerProtocol::SECPLUS1) return one_.receiver().door();
    if (mode_ == OpenerProtocol::SECPLUS2) return two_.receiver().door();
    return DoorState::UNKNOWN;
  }
  std::optional<bool> light() const {
    if (mode_ == OpenerProtocol::SECPLUS1) return one_.receiver().light();
    if (mode_ == OpenerProtocol::SECPLUS2) return two_.receiver().light();
    return {};
  }
  std::optional<bool> locked() const {
    if (mode_ == OpenerProtocol::SECPLUS1) return one_.receiver().locked();
    if (mode_ == OpenerProtocol::SECPLUS2) return two_.receiver().locked();
    return {};
  }
  bool controls_available() const {
    if (mode_ == OpenerProtocol::SECPLUS1) return one_.controls_available();
    if (mode_ == OpenerProtocol::SECPLUS2) return two_.controls_available();
    return false;
  }
  bool releasing() const {
    return (mode_ == OpenerProtocol::SECPLUS1 && one_.releasing()) ||
           (mode_ == OpenerProtocol::SECPLUS2 && two_.releasing());
  }
  bool command_idle(uint32_t now) const {
    return (mode_ == OpenerProtocol::SECPLUS1 && one_.command_idle(now)) ||
           (mode_ == OpenerProtocol::SECPLUS2 && two_.command_idle(now));
  }
  bool set_light(uint32_t now, bool target) {
    // Sec+ 1.0 only has a toggle; retain a fresh differing baseline even at dispatch.
    tick(now);
    if (mode_ == OpenerProtocol::SECPLUS1)
      return light().has_value() && *light() != target && one_.press_light(now);
    return mode_ == OpenerProtocol::SECPLUS2 && two_.set_light(now, target);
  }
  bool set_lock(uint32_t now, bool target) {
    if (mode_ == OpenerProtocol::SECPLUS1) return one_.set_lock(now, target);
    return mode_ == OpenerProtocol::SECPLUS2 && two_.set_lock(now, target);
  }
  uint32_t light_commands() const {
    return mode_ == OpenerProtocol::SECPLUS1 ? one_.light_commands() :
        mode_ == OpenerProtocol::SECPLUS2 ? two_.light_commands() : 0;
  }
  uint32_t lock_commands() const {
    return mode_ == OpenerProtocol::SECPLUS1 ? one_.lock_commands() :
        mode_ == OpenerProtocol::SECPLUS2 ? two_.lock_commands() : 0;
  }
 protected:
  Secplus1Transport &one_;
  Secplus2Transport &two_;
  OpenerProtocol mode_{OpenerProtocol::UNCONFIGURED};
  bool bound_{false};
};

class UnifiedOutputs : public ControlOutputs {
 public:
  UnifiedOutputs(Secplus1Transport &one, Secplus2Transport &two) : one_(one), two_(two) {}
  void setup(OpenerProtocol mode, InternalGPIOPin *door, InternalGPIOPin *buzzer) {
    if (initialized_) return;
    initialized_ = true;
    // Common inactive state, before *any* UART or pulse owner is initialized.
    door->digital_write(false); door->pin_mode(gpio::FLAG_OUTPUT);
    buzzer->digital_write(false); buzzer->pin_mode(gpio::FLAG_OUTPUT);
    switch (mode) {
      case OpenerProtocol::PULSE: pulse_.setup(door, buzzer); active_ = &pulse_; break;
      case OpenerProtocol::SECPLUS1: one_.setup(buzzer); active_ = &one_; break;
      case OpenerProtocol::SECPLUS2: two_.setup(buzzer); active_ = &two_; break;
      default: break;
    }
  }
  void warning_start(uint32_t ms) override { cancel_audio_(); if (active_) active_->warning_start(ms); }
  bool warning_tick(uint32_t ms) override { return active_ && active_->warning_tick(ms); }
  void warning_stop() override { cancel_audio_(); if (active_) active_->warning_stop(); }
  bool pulse(uint32_t ms) override { cancel_audio_(); return active_ && active_->pulse(ms); }
  bool toggle(uint32_t ms, DoorState expected) override { cancel_audio_(); return active_ && active_->toggle(ms, expected); }
  bool directed(uint32_t ms, bool open, DoorState expected) override {
    cancel_audio_();
    return active_ && active_->directed(ms, open, expected);
  }
  bool supports_stopped_direction() const override { return active_ && active_->supports_stopped_direction(); }
  bool reports_motion() const override { return active_ && active_->reports_motion(); }
  bool pulse_active() const override { return active_ && active_->pulse_active(); }
  void recovery_feedback(bool factory) {
    cancel_audio_();
    // Parent stops all protocol/control work first. Finite buzzer-only feedback.
    if (initialized_) tone(13, factory ? 2000 : 1000, factory ? 300 : 100);
  }
  bool report_ip(uint32_t now, const char *ip) {
    startup_.cancel(); return initialized_ && ip_reporter_.start(now, ip);
  }
  void startup_tune(uint32_t now, StartupTune tune) {
    if (initialized_ && !ip_reporter_.active()) startup_.start(now,tune);
  }
  void service_startup(uint32_t now, bool allowed) {
    if (allowed) startup_.tick(now); else startup_.cancel();
  }
  void service_ip(uint32_t now, bool allowed) {
    if (allowed) ip_reporter_.tick(now); else ip_reporter_.cancel();
  }
  void stop() override { cancel_audio_(); if (active_) active_->stop(); }
  void stop_for_readiness() override {
    // Diagnostic audio cannot overlap our warning/pulse: dispatch preempts it.
    // Still stop the actuator/transport; do not mute GPIO13 owned by the reporter.
    if (!ip_reporter_.active() && !startup_.active()) { stop(); return; }
    if (active_) active_->stop_for_readiness();
  }
 protected:
  void cancel_audio_() { ip_reporter_.cancel(); startup_.cancel(); }
  StartupMelody startup_;
  IPReporter ip_reporter_;
  PulseOutputs pulse_;
  Secplus1Outputs one_;
  Secplus2Outputs two_;
  ControlOutputs *active_{nullptr};
  bool initialized_{false};
};
using UnifiedCover = Secplus2Cover;
using UnifiedLight = Secplus2Light;
using UnifiedLightIntent = TargetLightIntent<UnifiedPort>;
}  // namespace esphome::opengarage
#endif
