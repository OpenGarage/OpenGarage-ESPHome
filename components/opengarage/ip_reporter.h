// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Arduino.h>
#include <cstdint>
#include <cstring>

namespace esphome::opengarage {
// Stock OG melody, serviced without delays or a catch-up loop. Every tone has
// a finite hardware-timer duration, including if the application stops servicing us.
class IPReporter {
 public:
  bool start(uint32_t now, const char *ip) {
    cancel();
    if (!ip || std::strlen(ip) > 15) return false;
    unsigned parts = 0, digits = 0, value = 0, nonzero = 0;
    for (const char *p = ip;; ++p) {
      if (*p >= '0' && *p <= '9') {
        if (++digits > 3) return false;
        value = value * 10 + unsigned(*p - '0');
        if (value > 255) return false;
      } else if (*p == '.' || !*p) {
        if (!digits || ++parts > 4) return false;
        nonzero |= value; digits = value = 0;
        if (!*p) break;
      } else return false;
    }
    if (parts != 4 || !nonzero) return false;
    std::strcpy(address_, ip);
    active_ = true; index_ = note_ = 0; stage_ = Stage::CHAR;
    at_ = now; wait_ = 0; tick(now);
    return true;
  }
  void cancel() {
    if (active_) noTone(13); // Never silence another owner when already inactive.
    active_ = false;
  }
  bool active() const { return active_; }
  void tick(uint32_t now) {
    const uint32_t elapsed = uint32_t(now - at_);
    // Short audio intervals use modular time ordering. A stale timestamp is
    // not a multi-week elapsed interval; ignore it without advancing the note.
    if (!active_ || elapsed >= 0x80000000U || elapsed < wait_) return;
    at_ = now;
    if (stage_ == Stage::END) { cancel(); return; }
    if (stage_ == Stage::DOT_GAP) {
      noTone(13); wait_ = 1000; stage_ = Stage::CHAR; ++index_; return;
    }
    const char c = address_[index_];
    if (!c) {
      tone(13, 1047, 1000); wait_ = 1000; stage_ = Stage::END;
    } else if (c == '.') {
      tone(13, 523, 500); wait_ = 500; stage_ = Stage::DOT_GAP;
    } else {
      const unsigned count = c == '0' ? 10 : unsigned(c - '0');
      if (note_ < count) {
        static constexpr unsigned notes[] = {262,277,294,311,330,349,370,392,415,440};
        tone(13, notes[note_++], 500); wait_ = 500;
      } else {
        noTone(13); wait_ = 1100; note_ = 0; ++index_;
      }
    }
  }
 protected:
  enum class Stage : uint8_t { CHAR, DOT_GAP, END };
  char address_[16]{};
  uint32_t at_{0}, wait_{0};
  unsigned index_{0}, note_{0};
  Stage stage_{Stage::CHAR};
  bool active_{false};
};
}  // namespace esphome::opengarage
