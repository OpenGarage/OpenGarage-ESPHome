// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace esphome::opengarage {

enum class FilterMode : uint8_t { CONSENSUS, MEDIAN };
enum class TimeoutPolicy : uint8_t { IGNORE, CAP };

class DistanceFilter {
 public:
  static constexpr uint32_t ECHO_TIMEOUT_US = 26000;
  static constexpr size_t SAMPLE_COUNT = 7;
  static constexpr size_t CONSENSUS_COUNT = 5;
  void configure(FilterMode mode, TimeoutPolicy timeout, uint16_t margin_cm, uint32_t stale_ms) {
    mode_ = mode;
    timeout_ = timeout;
    margin_us_ = std::max<uint32_t>(60, uint32_t(margin_cm) * 100000 / 1716);
    stale_ms_ = stale_ms;
  }

  void push(uint32_t pulse_us, uint32_t now_ms) {
    if (count_ && uint32_t(now_ms - last_sample_ms_) >= stale_ms_) count_ = cursor_ = 0;
    const bool measured = pulse_us > 0 && pulse_us <= ECHO_TIMEOUT_US;
    if (measured) {
      last_echo_ms_ = now_ms;
      have_echo_ = true;
    } else {
      ++timeout_count_;
      if (timeout_ == TimeoutPolicy::IGNORE) return;
      pulse_us = ECHO_TIMEOUT_US;
    }
    samples_[cursor_] = pulse_us;
    cursor_ = (cursor_ + 1) % SAMPLE_COUNT;
    if (count_ < SAMPLE_COUNT) ++count_;
    last_sample_ms_ = now_ms;
    if (count_ < SAMPLE_COUNT) return;

    auto sorted = samples_;
    std::sort(sorted.begin(), sorted.end());
    uint32_t filtered;
    if (mode_ == FilterMode::MEDIAN) {
      filtered = sorted[SAMPLE_COUNT / 2];
    } else {
      // The tightest five are consecutive in sorted order. Strict comparison
      // keeps the first (lower-distance) group on a tie, independent of arrival order.
      size_t best = 0;
      uint32_t span = sorted[CONSENSUS_COUNT - 1] - sorted[0];
      for (size_t start = 1; start <= SAMPLE_COUNT - CONSENSUS_COUNT; ++start) {
        const uint32_t candidate = sorted[start + CONSENSUS_COUNT - 1] - sorted[start];
        if (candidate < span) { best = start; span = candidate; }
      }
      if (span > margin_us_) return;
      uint32_t sum = 0;
      for (size_t i = best; i < best + CONSENSUS_COUNT; ++i) sum += sorted[i];
      filtered = sum / CONSENSUS_COUNT;
    }
    const auto cm = static_cast<uint16_t>(filtered * 1716 / 100000);
    if (cm == 0 || cm > 500) return;
    last_result_ = cm;
    accepted_ms_ = now_ms;
  }

  std::optional<uint16_t> distance(uint32_t now_ms) const {
    // Even CAP cannot keep disconnected hardware looking healthy indefinitely.
    if (!last_result_ || !have_echo_ || uint32_t(now_ms - accepted_ms_) >= stale_ms_ ||
        uint32_t(now_ms - last_echo_ms_) >= stale_ms_) return std::nullopt;
    return last_result_;
  }
  uint32_t timeout_count() const { return timeout_count_; }
  size_t sample_count() const { return count_; }

 protected:
  std::array<uint32_t, SAMPLE_COUNT> samples_{};
  size_t cursor_{0}, count_{0};
  FilterMode mode_{FilterMode::CONSENSUS};
  TimeoutPolicy timeout_{TimeoutPolicy::IGNORE};
  uint32_t margin_us_{582}, stale_ms_{10000}, accepted_ms_{0}, last_sample_ms_{0}, last_echo_ms_{0};
  uint32_t timeout_count_{0};
  bool have_echo_{false};
  std::optional<uint16_t> last_result_;
};

}  // namespace esphome::opengarage
