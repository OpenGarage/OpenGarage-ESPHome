// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/defines.h"
#ifdef USE_OPENGARAGE_THRESHOLDS
#include "esphome/components/number/number.h"
#include <cmath>
#include <cstdint>

namespace esphome::opengarage {
struct ThresholdSettings {
  static constexpr uint16_t MAX_CM = 450;  // Stock OG's documented nominal sensor cap.
  uint16_t door{50}, vehicle{150};
  static constexpr uint32_t KEY = 0x4F475401U;  // OG thresholds, format 1.
  uint32_t encode() const { return 0x4F480000U | uint32_t(door) | (uint32_t(vehicle) << 9); }
  bool decode(uint32_t stored) {
    const auto d = stored & 0x1FFU, v = (stored >> 9) & 0x1FFU;
    if ((stored & 0xFFFC0000U) != 0x4F480000U || d < 1 || d > MAX_CM || v > MAX_CM) return false;
    door = d; vehicle = v; return true;
  }
  static bool valid_input(bool door, float value) {
    return std::isfinite(value) && value >= (door ? 1 : 0) && value <= MAX_CM && std::floor(value) == value;
  }
};

class ThresholdCommands {
 public:
  virtual ~ThresholdCommands() = default;
  virtual void request_threshold(bool door, float value) = 0;
};

// The parent validates, persists and applies first. Never optimistic; rejected
// calls republish the current value. No YAML action can bypass the parent.
class ThresholdNumber : public number::Number {
 public:
  ThresholdNumber(ThresholdCommands *parent, bool door) : parent_(parent), door_(door) {}
 protected:
  void control(float value) override { parent_->request_threshold(door_, value); }
  ThresholdCommands *parent_;
  bool door_;
};
}  // namespace esphome::opengarage
#endif
