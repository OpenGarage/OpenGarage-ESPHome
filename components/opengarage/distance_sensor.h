// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "distance_filter.h"
#include "esphome/core/gpio.h"

namespace esphome::opengarage {

struct EchoCapture {
  ISRInternalGPIOPin pin;
  volatile bool armed{false}, rising{false}, done{false};
  volatile uint32_t trigger_us{0}, rise_us{0}, width_us{0};
  static void gpio_intr(EchoCapture *capture);
};

class DistanceSensor {
 public:
  void set_pins(InternalGPIOPin *trigger, InternalGPIOPin *echo) { trigger_ = trigger; echo_ = echo; }
  void configure(FilterMode mode, TimeoutPolicy policy, uint16_t margin, uint32_t stale, uint32_t interval) {
    filter_.configure(mode, policy, margin, stale);
    interval_ms_ = interval;
  }
  void setup();
  void loop(uint32_t now_ms);
  void shutdown();
  std::optional<uint16_t> distance(uint32_t now_ms) const { return filter_.distance(now_ms); }
  uint32_t timeout_count() const { return filter_.timeout_count(); }

 protected:
  InternalGPIOPin *trigger_{nullptr}, *echo_{nullptr};
  EchoCapture capture_;
  DistanceFilter filter_;
  uint32_t interval_ms_{500}, last_trigger_ms_{0};
  bool started_{false}, pending_{false};
};

}  // namespace esphome::opengarage
