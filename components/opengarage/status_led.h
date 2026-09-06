// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "esphome/core/gpio.h"
#include <Ticker.h>
#include <cstdint>

namespace esphome::opengarage {

// ESP8266 status only: OFF by default; optional nominal 1 ms heartbeat.
class StatusLed {
 public:
  static constexpr uint32_t PULSE_MS = 1;
  static constexpr uint32_t INTERVAL_MS = 1000;

  void set_enabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) {
      off_timer_.detach();
      if (ready_) pin_->digital_write(inverted_);
    }
  }

  bool enabled() const { return enabled_; }

  void setup(InternalGPIOPin *pin, bool inverted, uint32_t now) {
    stop();
    pin_ = pin;
    inverted_ = inverted;
    last_blink_ms_ = now;
    pin_->digital_write(inverted_);  // Preload OFF before enabling the output.
    pin_->pin_mode(gpio::FLAG_OUTPUT);
    ready_ = true;
  }

  void loop(uint32_t now) {
    if (!ready_ || !enabled_ || uint32_t(now - last_blink_ms_) < INTERVAL_MS) return;
    last_blink_ms_ = now;  // No catch-up flashes after a delayed loop.
    pin_->digital_write(!inverted_);
    // Plain once_ms runs in the SDK timer context, not a later ESPHome loop.
    // Callback does GPIO only: no publication, logging or network work.
    off_timer_.once_ms(PULSE_MS, [this]() {
      if (ready_) pin_->digital_write(inverted_);
    });
  }

  void stop() {
    off_timer_.detach();
    if (ready_) pin_->digital_write(inverted_);
    ready_ = false;
  }

 protected:
  InternalGPIOPin *pin_{nullptr};
  Ticker off_timer_;
  uint32_t last_blink_ms_{0};
  bool inverted_{true}, ready_{false}, enabled_{false};
};

}  // namespace esphome::opengarage
