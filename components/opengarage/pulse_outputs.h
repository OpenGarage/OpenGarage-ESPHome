// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "action_controller.h"
#include "esphome/core/gpio.h"
#include <Arduino.h>
#include <Ticker.h>

namespace esphome::opengarage {

// Exclusive owner of GPIO15 and GPIO13 in the explicit pulse-control builds.
// SDK timer does only GPIO LOW + a flag; no dispatch, publication or networking.
class PulseOutputs : public ControlOutputs {
 public:
  void setup(InternalGPIOPin *door, InternalGPIOPin *buzzer) {
    stop();
    door_ = door; buzzer_ = buzzer;
    door_->digital_write(false);
    door_->pin_mode(gpio::FLAG_OUTPUT);
    buzzer_->digital_write(false);
    buzzer_->pin_mode(gpio::FLAG_OUTPUT);
    ready_ = true;
  }
  void warning_start(uint32_t minimum_ms) override {
    warning_stop();
    if (!ready_) return;
    warning_ = true;
    last_burst_ = 0;
    bursts_ = 1;
    required_bursts_ = (minimum_ms + 999) / 1000;
    tone(13, 1000, 500);  // Independent finite-duration 1 kHz burst, stock envelope.
  }
  bool warning_tick(uint32_t elapsed) override {
    if (!ready_ || !warning_ || uint32_t(elapsed - last_burst_) < 1000) return false;
    if (bursts_ >= required_bursts_) return true;
    ++bursts_;
    last_burst_ = elapsed;
    // Late scheduling EXTENDS the countdown; never catch up with shortened beeps.
    // Finite tone ends independently after 500 ms, followed by at least 500 ms OFF.
    tone(13, 1000, 500);
    return false;
  }
  void warning_stop() override {
    warning_ = false;
    if (ready_) { noTone(13); buzzer_->digital_write(false); }
  }
  bool pulse(uint32_t duration) override {
    if (!ready_ || active_ || duration < 100 || duration > 1000) return false;
    active_ = true;
    off_.once_ms(duration, [this]() { door_->digital_write(false); active_ = false; });
    door_->digital_write(true);
    return true;
  }
  bool pulse_active() const override { return active_; }
  void stop_for_readiness() override {
    off_.detach();
    if (ready_) door_->digital_write(false);
    active_ = false;
    if (warning_) warning_stop();
  }
  void stop() override {
    off_.detach();
    if (ready_) door_->digital_write(false);
    active_ = false;
    warning_stop();
  }
 protected:
  InternalGPIOPin *door_{nullptr}, *buzzer_{nullptr};
  Ticker off_;
  uint32_t last_burst_{0}, bursts_{0}, required_bursts_{0};
  volatile bool active_{false};
  bool ready_{false}, warning_{false};
};

}  // namespace esphome::opengarage
