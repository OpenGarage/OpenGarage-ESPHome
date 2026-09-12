// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/defines.h"
#ifdef USE_OPENGARAGE_SECPLUS1
#include "state_resolver.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <optional>
#include "secplus1_trace.h"
#include "secplus1_gap.h"

namespace esphome::opengarage {

struct Secplus1Stats {
  uint32_t bytes{0}, frames{0}, door_frames{0}, light_lock_frames{0}, parity_errors{0};
  uint32_t partial_timeouts{0}, overflows{0}, ignored_bytes{0}, invalid_frames{0}, panel37{0};
  uint32_t tx_bytes{0}, tx_deferred{0}, tx_errors{0}, max_service_us{0};
  uint32_t deferred_backlog{0}, deferred_partial{0}, deferred_byte{0}, deferred_high{0};
  // Retired in 0.4.4: retain the existing optional diagnostic/entity at zero.
  // Do not repurpose its history as an RX interrupt or activity counter.
  uint32_t deferred_isr{0};
  uint16_t high_water{0};
};

// Bit 8 was the retired raw-wake veto; preserve the other diagnostic masks.
enum Secplus1TxBlock : uint8_t { SEC1_BACKLOG = 1, SEC1_PARTIAL = 2, SEC1_RECENT_BYTE = 4, SEC1_RX_HIGH = 16 };

inline bool secplus1_even_parity(uint8_t byte) {
  byte ^= byte >> 4; byte ^= byte >> 2; byte ^= byte >> 1;
  return (byte & 1) != 0; // Bit required to make data + parity have an even number of ones.
}

// Sec+ 1.0 wireline request/response framing; no RF codec or rolling counter.
class Secplus1Receiver {
 public:
  static constexpr uint32_t PARTIAL_TIMEOUT_MS = 100;
  void set_status_timeout(uint32_t ms) { timeout_ms_ = ms; }
  void tick(uint32_t now) {
    if (request_ && uint32_t(now - last_byte_ms_) >= PARTIAL_TIMEOUT_MS) {
      request_ = 0; gap_.reset(); inc_(stats_.partial_timeouts);
    }
    if (door_seen_ && uint32_t(now - door_ms_) >= timeout_ms_) clear_door_();
    if (light_seen_ && uint32_t(now - light_ms_) >= timeout_ms_) clear_light_();
    if (obstruction_ && uint32_t(now - obstruction_ms_) >= timeout_ms_) obstruction_.reset();
    if (door_count_ && uint32_t(now - door_candidate_ms_) >= timeout_ms_) door_count_ = 0;
    if (light_count_ && uint32_t(now - light_candidate_ms_) >= timeout_ms_) light_count_ = 0;
  }
  void feed(uint8_t byte, bool parity_bit, uint32_t now) {
    sec1_trace('R', now * 1000U, byte, parity_bit == secplus1_even_parity(byte) ? 1 : 0);
#ifdef USE_OPENGARAGE_SECPLUS1_RAW_TRACE
    // Development-only history, including rejected bytes. Never log in ISR.
    trace_[trace_next_] = {now, byte, parity_bit};
    trace_next_ = (trace_next_ + 1) % trace_.size();
    if (trace_count_ < trace_.size()) ++trace_count_;
#endif
    tick(now); last_byte_ms_ = now; inc_(stats_.bytes);
    if (parity_bit != secplus1_even_parity(byte)) {
      request_ = 0; gap_.reset(); door_count_ = light_count_ = 0; inc_(stats_.parity_errors); return;
    }
    if (request_) {
      const uint8_t request = request_;
      request_ = 0;
      // A command cannot be the expected status payload. Recover at this byte;
      // notably 0x31 is a panel/button release, not an Opening report.
      if (byte >= 0x30 && byte <= 0x3A) {
        gap_.reset(); inc_(stats_.invalid_frames); door_count_ = light_count_ = 0;
        start_request_(byte); return;
      }
      last_frame_ = (uint16_t(request) << 8) | byte;
      inc_(stats_.frames);
      if (request == 0x38) {
        const DoorState value = decode_door_(byte & 7);
        if (value == DoorState::UNKNOWN) { gap_.reset(); inc_(stats_.invalid_frames); clear_door_(); return; }
        inc_(stats_.door_frames); observed_status_ = true;
        if (value != door_candidate_) { door_candidate_ = value; door_count_ = 0; }
        door_candidate_ms_ = now;
        if (door_count_ < 2) ++door_count_;
        if (door_count_ == 2) { door_ = value; door_seen_ = true; door_ms_ = now; }
      } else if (request == 0x3A) {
        // Stock OpenGarage requires upper nibble 5 for this response.
        if ((byte & 0xF0) != 0x50) { gap_.reset(); inc_(stats_.invalid_frames); clear_light_(); return; }
        inc_(stats_.light_lock_frames); observed_status_ = true;
        const uint8_t value = byte & 0x0C;
        if (value != light_candidate_) { light_candidate_ = value; light_count_ = 0; }
        light_candidate_ms_ = now;
        if (light_count_ < 2) ++light_count_;
        if (light_count_ == 2) {
          light_ = (value & 4) != 0; locked_ = (value & 8) == 0;
          light_seen_ = true; light_ms_ = now;
        }
      } else if (request == 0x39) {
        // Stock compatibility: zero is clear, nonzero obstructed. Stock marks
        // this interpretation unproven; telemetry only, not a safety interlock.
        obstruction_ = byte != 0;
        obstruction_ms_ = now;
      }
      gap_.exchange(query_ms_, now);
      return;
    }
    start_request_(byte);
  }
  void transport_loss() {
    request_ = 0; gap_.reset(); clear_door_(); clear_light_(); obstruction_.reset();
  }
  void note_activity(uint32_t now) { last_byte_ms_ = now; }
  void note_overflow() { inc_(stats_.overflows); transport_loss(); }
  void note_depth(size_t bytes) { stats_.high_water = std::max(stats_.high_water, uint16_t(std::min<size_t>(bytes, UINT16_MAX))); }
  void note_service(uint32_t us) { stats_.max_service_us = std::max(stats_.max_service_us, us); }
  void note_tx(bool ok) { inc_(ok ? stats_.tx_bytes : stats_.tx_errors); }
  void note_deferred(uint8_t mask) {
    inc_(stats_.tx_deferred);
    if (mask & SEC1_BACKLOG) inc_(stats_.deferred_backlog);
    if (mask & SEC1_PARTIAL) inc_(stats_.deferred_partial);
    if (mask & SEC1_RECENT_BYTE) inc_(stats_.deferred_byte);
    if (mask & SEC1_RX_HIGH) inc_(stats_.deferred_high);
  }
  // Oldest to newest: boot-milliseconds:hex-byte/received-parity(+ correct, ! bad).
  // History is diagnostic only, not evidence of a valid/fresh protocol response.
  void format_trace(char *out, size_t capacity) const {
    if (!capacity) return;
    out[0] = '\0';
#ifdef USE_OPENGARAGE_SECPLUS1_RAW_TRACE
    size_t used = 0;
    for (size_t n = 0; n < trace_count_; ++n) {
      const auto &sample = trace_[(trace_next_ + trace_.size() - trace_count_ + n) % trace_.size()];
      const int length = std::snprintf(out + used, capacity - used, "%s%lu:%02X/%u%c", n ? " " : "",
          static_cast<unsigned long>(sample.ms), unsigned(sample.byte), unsigned(sample.parity),
          sample.parity == secplus1_even_parity(sample.byte) ? '+' : '!');
      if (length < 0 || size_t(length) >= capacity - used) return;
      used += size_t(length);
    }
#endif
  }
  bool valid() const { return door_seen_; }
  DoorState door() const { return door_; }
  std::optional<bool> light() const { return light_; }
  std::optional<bool> locked() const { return locked_; }
  std::optional<bool> obstructed() const { return obstruction_; }
  bool partial() const { return request_ != 0; }
  bool gap_ready(uint32_t now) const { return gap_.ready(now); }
  bool gap_timing_insufficient(uint32_t now) const { return gap_.timing_insufficient(now); }
  void reset_gap() { gap_.reset(); }
  bool observed_status() const { return observed_status_; }
  bool panel37_seen() const { return stats_.panel37 != 0; }
  uint32_t last_byte_ms() const { return last_byte_ms_; }
  std::optional<uint16_t> last_frame() const { return last_frame_; }
  const Secplus1Stats &stats() const { return stats_; }

 protected:
  static void inc_(uint32_t &v) { if (v != UINT32_MAX) ++v; }
  static DoorState decode_door_(uint8_t value) {
    switch (value) {
      case 0: case 6: return DoorState::STOPPED;
      case 1: return DoorState::OPENING;
      case 2: return DoorState::OPEN;
      case 4: return DoorState::CLOSING;
      case 5: return DoorState::CLOSED;
      default: return DoorState::UNKNOWN;
    }
  }
  void start_request_(uint8_t byte) {
    if (byte == 0x38 || byte == 0x39 || byte == 0x3A) { request_ = byte; query_ms_ = last_byte_ms_; }
    else { gap_.reset(); inc_(stats_.ignored_bytes); if (byte == 0x37) inc_(stats_.panel37); }
  }
  void clear_door_() { door_seen_ = false; door_count_ = 0; door_ = DoorState::UNKNOWN; }
  void clear_light_() { light_seen_ = false; light_count_ = 0; light_.reset(); locked_.reset(); }
  uint8_t request_{0}, door_count_{0}, light_count_{0}, light_candidate_{0};
  Secplus1Gap gap_;
  uint32_t query_ms_{0};
  DoorState door_{DoorState::UNKNOWN}, door_candidate_{DoorState::UNKNOWN};
  std::optional<bool> light_, locked_, obstruction_;
  std::optional<uint16_t> last_frame_;
  uint32_t last_byte_ms_{0}, door_ms_{0}, light_ms_{0}, door_candidate_ms_{0}, light_candidate_ms_{0};
  uint32_t obstruction_ms_{0};
  uint32_t timeout_ms_{10000};
  bool door_seen_{false}, light_seen_{false}, observed_status_{false};
  Secplus1Stats stats_;
#ifdef USE_OPENGARAGE_SECPLUS1_RAW_TRACE
  struct RawSample { uint32_t ms; uint8_t byte; bool parity; };
  std::array<RawSample, 8> trace_{};
  size_t trace_next_{0}, trace_count_{0};
#endif
};

enum class Secplus1PanelState : uint8_t { NOT_STARTED, PASSIVE, WAITING, EXISTING_PANEL, EMULATING, UNSUPPORTED_PANEL, TX_FAULT, STOPPED, WAITING_NO_EMULATION };
inline const char *secplus1_panel_name(Secplus1PanelState state) {
  switch (state) {
    case Secplus1PanelState::PASSIVE: return "Passive listening";
    case Secplus1PanelState::WAITING: return "Waiting for wall panel";
    case Secplus1PanelState::WAITING_NO_EMULATION: return "Emulation disabled; waiting for panel status";
    case Secplus1PanelState::EXISTING_PANEL: return "Existing wall panel; no polling";
    case Secplus1PanelState::EMULATING: return "Emulating wall panel";
    case Secplus1PanelState::UNSUPPORTED_PANEL: return "0x37 panel; active support deferred";
    case Secplus1PanelState::TX_FAULT: return "Polling TX fault; reboot required";
    case Secplus1PanelState::STOPPED: return "Stopped";
    default: return "Receiver not started";
  }
}

// Explicit opt-in only. Never generates door/light/lock PRESS commands. This is
// a bounded scheduler, not a protocol detector or general-purpose command queue.
class Secplus1Panel {
 public:
  void start(bool transmit, uint32_t now, bool emulate_if_needed = true) {
    state_ = !transmit ? Secplus1PanelState::PASSIVE : emulate_if_needed ?
        Secplus1PanelState::WAITING : Secplus1PanelState::WAITING_NO_EMULATION;
    started_ms_ = sent_ms_ = now; index_ = 0;
  }
  void tick(uint32_t now, bool observed_status, bool panel37) {
    if (state_ == Secplus1PanelState::STOPPED || state_ == Secplus1PanelState::NOT_STARTED || state_ == Secplus1PanelState::TX_FAULT) return;
    if (panel37) { state_ = Secplus1PanelState::UNSUPPORTED_PANEL; return; }
    if (state_ != Secplus1PanelState::WAITING && state_ != Secplus1PanelState::WAITING_NO_EMULATION) return;
    if (observed_status) state_ = Secplus1PanelState::EXISTING_PANEL;
    else if (state_ == Secplus1PanelState::WAITING && uint32_t(now - started_ms_) >= 20000)
      state_ = Secplus1PanelState::EMULATING;
  }
  std::optional<uint8_t> due(uint32_t now) const {
    if (state_ != Secplus1PanelState::EMULATING || uint32_t(now - sent_ms_) < 250) return {};
    // Wire bytes used by stock garagelib / gdolib. See VENDOR.md. Their last
    // array element is never reached; preserve the observed 15..17 repeat cycle.
    constexpr uint8_t sequence[] = {0x35,0x35,0x35,0x35,0x33,0x33,0x53,0x53,0x38,0x3A,0x3A,0x3A,0x39,0x38,0x3A,0x38,0x3A,0x39};
    return sequence[index_];
  }
  void sent(uint32_t now, bool ok) {
    if (!ok) { state_ = Secplus1PanelState::TX_FAULT; return; }
    sent_ms_ = now; if (++index_ == 18) index_ = 15;
  }
  void stop() { state_ = Secplus1PanelState::STOPPED; }
  bool commands_ready() const {
    return state_ == Secplus1PanelState::EXISTING_PANEL ||
        (state_ == Secplus1PanelState::EMULATING && index_ >= 15);
  }
  void defer_poll(uint32_t now) { sent_ms_ = now; }  // No catch-up poll after an action.
  Secplus1PanelState state() const { return state_; }
 protected:
  Secplus1PanelState state_{Secplus1PanelState::NOT_STARTED};
  uint32_t started_ms_{0}, sent_ms_{0};
  uint8_t index_{0};
};

template<class Uart, class MicroClock>
bool pump_secplus1(Uart &uart, Secplus1Receiver &receiver, uint32_t now, MicroClock clock) {
  const uint32_t start = clock();
  receiver.tick(now);
  const int available = uart.available();
  const size_t waiting = available > 0 ? size_t(available) : 0;
  receiver.note_depth(waiting);
  if (uart.overflow()) {
    receiver.note_overflow(); receiver.note_activity(now); uart.discard_rx();
    receiver.note_service(uint32_t(clock() - start)); return false;
  }
  size_t count = 0;
  while (count < waiting && count < 32 && uint32_t(clock() - start) < 1000) {
    const int value = uart.read();
    if (value < 0) break;
    receiver.feed(uint8_t(value), uart.readParity(), now); ++count;
  }
  receiver.note_service(uint32_t(clock() - start));
  return count < waiting;
}

} // namespace esphome::opengarage
#endif
