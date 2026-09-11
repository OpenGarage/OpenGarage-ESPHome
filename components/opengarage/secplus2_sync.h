// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/defines.h"
#ifdef USE_OPENGARAGE_SECPLUS2_SYNC
#include "secplus2_rx.h"

namespace esphome::opengarage {

// Lab query session. Actuation encoders exist only with the separate control
// opt-in; the query-only image cannot encode door/light/lock/learn commands.
// A received response after a query is evidence of observed traffic, NOT an
// authenticated acknowledgement that this client's rolling sequence was accepted.
class Secplus2QuerySession {
 public:
  enum class State { STOPPED, SEEKING, OBSERVED, TIMED_OUT, WRITE_FAILED };
  static constexpr uint32_t ROLLING_MASK = 0x0FFFFFFF;
  static constexpr uint16_t GET_STATUS = 0x080, GET_OPENINGS = 0x48B;
  void start(uint32_t client, uint32_t now) {
    if (state_ != State::STOPPED || !client) return;
    client_ = client;
    rolling_ = 0; // Explicit stock_zero_on_boot experiment, no flash writes.
    state_ = State::SEEKING;
    started_ms_ = last_attempt_ms_ = now;
  }
  void tick(uint32_t now, const Secplus2Receiver &rx) {
    if (state_ != State::SEEKING && state_ != State::OBSERVED) return;
    if (status_sent_ && rx.valid() && rx.stats().status_frames != status_baseline_) status_seen_ = true;
    if (openings_sent_ && rx.openings().has_value() && rx.openings_frames() != openings_baseline_) openings_seen_ = true;
    if (state_ == State::OBSERVED && !rx.status_link_fresh()) {
      state_ = State::SEEKING; started_ms_ = now;
      status_seen_ = openings_seen_ = status_sent_ = openings_sent_ = false;
      next_openings_ = false;
    }
    if (status_seen_ && openings_seen_ && rx.valid()) state_ = State::OBSERVED;
    else if (state_ == State::SEEKING && uint32_t(now - started_ms_) >= 30000) state_ = State::TIMED_OUT;
  }
  std::optional<uint16_t> due(uint32_t now) const {
    if (state_ != State::SEEKING && state_ != State::OBSERVED) return {};
    const uint32_t gap = state_ == State::OBSERVED ? 5000 : 500;
    if (uint32_t(now - last_attempt_ms_) < gap) return {};
    return state_ == State::OBSERVED || !next_openings_ ? GET_STATUS : GET_OPENINGS;
  }
  bool encode_query(uint16_t command, uint8_t *packet) const {
    if (!client_ || (command != GET_STATUS && command != GET_OPENINGS)) return false;
    const uint64_t fixed = uint64_t(client_) | (uint64_t(command & 0xF00U) << 24);
    return encode_wireline(rolling_, fixed, command & 0xFFU, packet) == 0;
  }
  void sent(uint16_t command, uint32_t now, bool ok, const Secplus2Receiver &rx) {
    last_attempt_ms_ = now;
    // Consume even a partial write. Never reuse a possibly transmitted code.
    rolling_ = (rolling_ + 1) & ROLLING_MASK;
    if (!ok) { state_ = State::WRITE_FAILED; return; }
    if (command == GET_STATUS && !status_sent_) {
      status_sent_ = true; status_baseline_ = rx.stats().status_frames;
    }
    if (command == GET_OPENINGS && !openings_sent_) {
      openings_sent_ = true; openings_baseline_ = rx.openings_frames();
    }
    next_openings_ = !next_openings_;
  }
  void defer(uint32_t now) { last_attempt_ms_ = now; } // No catch-up or per-loop contention storm.
#ifdef USE_OPENGARAGE_SECPLUS2_CONTROL
  // Only the control opt-in can encode actuations. No raw command,
  // Stop or learn entrypoint. Cleanup may encode a release after a failed
  // press write, but the transport forbids starting a new action in that state.
  bool encode_door(bool open, bool pressed, uint8_t *packet) const {
    return encode_control_(0x280, (uint32_t(open) << 8) | 0x01000000U |
                                    (uint32_t(pressed) << 16), packet);
  }
  bool encode_toggle(bool pressed, uint8_t *packet) const {
    // Canonical Security+ 2.0 door action 2: one wall-button-style Toggle.
    return encode_control_(0x280, 0x01000200U | (uint32_t(pressed) << 16), packet);
  }
  bool encode_light(bool on, uint8_t *packet) const {
    return encode_control_(0x281, uint32_t(on) << 8, packet);
  }
  bool encode_lock(bool locked, uint8_t *packet) const {
    return encode_control_(0x18C, uint32_t(locked) << 8, packet);
  }
  void control_sent(uint32_t now, bool ok) {
    last_attempt_ms_ = now;
    rolling_ = (rolling_ + 1) & ROLLING_MASK;
    if (!ok) state_ = State::WRITE_FAILED;
  }
#endif
  void stop() { state_ = State::STOPPED; }
  State state() const { return state_; }
  uint32_t rolling() const { return rolling_; }
  uint32_t client() const { return client_; }
  const char *state_name() const {
    switch (state_) {
      case State::SEEKING: return "Querying status/openings";
      case State::OBSERVED: return "Responses observed (not TX acknowledgement)";
      case State::TIMED_OUT: return "Sync timed out; reboot to retry";
      case State::WRITE_FAILED: return "TX failed; reboot required";
      default: return "Queries stopped";
    }
  }
 protected:
#ifdef USE_OPENGARAGE_SECPLUS2_CONTROL
  bool encode_control_(uint16_t command, uint32_t payload, uint8_t *packet) const {
    if (!client_) return false;
    return encode_wireline(rolling_, uint64_t(client_) | (uint64_t(command & 0xF00U) << 24),
                           payload | (command & 0xFFU), packet) == 0;
  }
#endif
  State state_{State::STOPPED};
  uint32_t client_{0}, rolling_{0}, started_ms_{0}, last_attempt_ms_{0};
  uint32_t status_baseline_{0}, openings_baseline_{0};
  bool next_openings_{false}, status_sent_{false}, openings_sent_{false};
  bool status_seen_{false}, openings_seen_{false};
};

} // namespace esphome::opengarage
#endif
