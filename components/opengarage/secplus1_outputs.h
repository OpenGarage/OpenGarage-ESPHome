// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/defines.h"
#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
#include "action_controller.h"
#include "secplus1_transport.h"
#include "esphome/core/hal.h"
#include <Arduino.h>

namespace esphome::opengarage {
// Buzzer owner only. GPIO15 always belongs to Secplus1Transport, never PulseOutputs.
class Secplus1Outputs : public ControlOutputs {
 public:
  explicit Secplus1Outputs(Secplus1Transport &port) : port_(port) {}
  void setup(InternalGPIOPin *buzzer) {
    buzzer_ = buzzer;
    buzzer_->digital_write(false); buzzer_->pin_mode(gpio::FLAG_OUTPUT);
  }
  void warning_start(uint32_t minimum_ms) override {
    warning_stop();
    if (!buzzer_) return;
    warning_ = true; last_burst_ = 0; bursts_ = 1;
    required_ = (minimum_ms + 999) / 1000;
    complete_at_.reset();
    tone(13, 1000, 500);  // Same stock envelope as the pulse MVP.
  }
  bool warning_tick(uint32_t elapsed) override {
    if (!buzzer_ || !warning_) return false;
    if (uint32_t(elapsed - last_burst_) < 1000) return false;
    if (bursts_ < required_) {
      ++bursts_; last_burst_ = elapsed; tone(13, 1000, 500); return false;
    }
    if (!complete_at_) complete_at_ = elapsed;
    // Extend the completed warning briefly for a bus-idle slot, never queue a
    // press for later. At expiry pulse() fails immediately if still busy.
    return port_.command_idle(millis()) || uint32_t(elapsed - *complete_at_) >= 1000;
  }
  void warning_stop() override {
    warning_ = false;
    if (buzzer_) { noTone(13); buzzer_->digital_write(false); }
  }
  bool pulse(uint32_t) override { return buzzer_ && port_.press_door(millis()); }
  bool pulse_active() const override { return port_.releasing(); }
  void stop() override { warning_stop(); port_.cancel_press(); }
 protected:
  Secplus1Transport &port_;
  InternalGPIOPin *buzzer_{nullptr};
  std::optional<uint32_t> complete_at_;
  uint32_t last_burst_{0}, bursts_{0}, required_{0};
  bool warning_{false};
};
}  // namespace esphome::opengarage
#endif
