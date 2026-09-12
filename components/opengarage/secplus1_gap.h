// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
namespace esphome::opengarage {
// Predictor, NOT bus arbitration. Dequeue timestamps cannot prove future silence.
// Require three consistent intervals and 160 ms of predicted room (140 + 20 margin).
class Secplus1Gap {
 public:
  void reset() { seen_ = false; count_ = 0; }
  void exchange(uint32_t query, uint32_t reply) {
    if (uint32_t(reply-query) < 10 || uint32_t(reply-query) > 60) { reset(); return; }
    if (seen_) {
      const uint32_t interval = query-query_;
      if (interval < 180 || interval > 400 ||
          (count_ && (interval + 20 < minimum_ || interval > maximum_ + 20))) count_ = 0;
      if (interval >= 180 && interval <= 400) {
        if (!count_) minimum_ = maximum_ = interval;
        else { minimum_ = std::min(minimum_, interval); maximum_ = std::max(maximum_, interval); }
        if (maximum_-minimum_ > 20) { minimum_=maximum_=interval; count_=0; }
        if (count_ < 3) ++count_;
      }
    }
    seen_=true; query_=query; reply_=reply;
  }
  bool ready(uint32_t now) const {
    const uint32_t age=now-query_, reply_age=now-reply_;
    return seen_ && count_>=3 && reply_age>=10 && reply_age<=45 &&
        age<minimum_ && minimum_-age>=160;
  }
  // Diagnostic only: distinguish an impossible window from one the loop missed.
  // At the earliest allowed start (reply + 10), 160 ms must still remain.
  // Do not blame panel timing after observations have gone stale.
  bool timing_insufficient(uint32_t now) const {
    return seen_ && count_>=3 && uint32_t(now-reply_)<=400 &&
        minimum_ < 170U + uint32_t(reply_-query_);
  }
 private:
  uint32_t query_{0}, reply_{0}, minimum_{0}, maximum_{0};
  uint8_t count_{0};
  bool seen_{false};
};
}
