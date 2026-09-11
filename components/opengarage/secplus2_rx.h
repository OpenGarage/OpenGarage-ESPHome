// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/defines.h"
#ifdef USE_OPENGARAGE_SECPLUS2_RX
#include "state_resolver.h"
#include "../opengarage_secplus_codec/secplus.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace esphome::opengarage {

struct Secplus2RxStats {
  uint32_t bytes{0}, frames{0}, status_frames{0}, decode_errors{0}, partial_timeouts{0};
  uint32_t overflows{0}, unknown_commands{0}, semantic_errors{0};
  uint16_t high_water{0};
  uint32_t max_service_us{0};
};

// Receive-only wireline framing/status model. No transport, identity or TX state.
class Secplus2Receiver {
 public:
  static constexpr size_t PACKET_SIZE = 19;
  static constexpr uint32_t PARTIAL_TIMEOUT_MS = 100;
  void set_status_timeout(uint32_t ms) { status_timeout_ms_ = ms; }
  void tick(uint32_t now) {
    if (used_ && uint32_t(now - last_byte_ms_) >= PARTIAL_TIMEOUT_MS) {
      used_ = 0;
      increment_(stats_.partial_timeouts);
    }
    if (status_seen_ && uint32_t(now - last_status_ms_) >= status_timeout_ms_) invalidate_status_();
    if (status_link_seen_ && uint32_t(now - last_status_link_ms_) >= status_timeout_ms_) status_link_seen_ = false;
  }
  void feed(uint8_t byte, uint32_t now) {
    tick(now);
    last_byte_ms_ = now;
    increment_(stats_.bytes);
    if (used_ < 3) {
      constexpr uint8_t prefix[] = {0x55, 0x01, 0x00};
      if (byte == prefix[used_]) packet_[used_++] = byte;
      else { used_ = byte == 0x55 ? 1 : 0; if (used_) packet_[0] = byte; }
      return;
    }
    packet_[used_++] = byte;
    if (used_ != PACKET_SIZE) return;
    uint32_t rolling = 0, data = 0;
    uint64_t fixed = 0;
    if (decode_wireline(packet_.data(), &rolling, &fixed, &data) < 0) {
      increment_(stats_.decode_errors);
      resync_();
      return;
    }
    used_ = 0;  // A valid payload may itself contain the prefix; never split it early.
    increment_(stats_.frames);
    const uint16_t command = ((fixed >> 24) & 0xF00U) | (data & 0xFFU);
#ifdef USE_OPENGARAGE_SECPLUS2_SYNC
    if (own_client_ && uint32_t(fixed) == own_client_) {
      increment_(stats_.unknown_commands); return; // Never treat our wire echo as a response.
    }
    if (command == 0x48C) {
      openings_ = uint16_t(((data >> 8) & 0xFF00U) | ((data >> 24) & 0xFFU));
      increment_(openings_frames_);
      return;
    }
#endif
    if (command != 0x081) { increment_(stats_.unknown_commands); return; }
    // A decoded opener response establishes link freshness even if its position
    // code is not recognized. Position validity remains independent.
    status_link_seen_ = true; last_status_link_ms_ = now;
    const auto door = decode_door_((data >> 8) & 0x0F);
    if (door == DoorState::UNKNOWN) {
      increment_(stats_.semantic_errors);
      invalidate_status_();
      return;
    }
    door_ = door;
    light_ = (data & (1UL << 25)) != 0;
    lock_ = (data & (1UL << 24)) != 0;
    obstruction_ = (data & (1UL << 22)) == 0;  // Stock's active-low STATUS bit.
    status_seen_ = true;
    last_status_ms_ = now;
    increment_(stats_.status_frames);
  }
  void transport_loss() { used_ = 0; status_link_seen_ = false; invalidate_status_(); }
  void note_overflow() { increment_(stats_.overflows); transport_loss(); }
  void note_depth(size_t bytes) {
    stats_.high_water = std::max(stats_.high_water, uint16_t(std::min<size_t>(bytes, UINT16_MAX)));
  }
  void note_service(uint32_t us) { stats_.max_service_us = std::max(stats_.max_service_us, us); }
  bool valid() const { return status_seen_; }
  bool status_link_fresh() const { return status_link_seen_; }
  DoorState door() const { return door_; }
  std::optional<bool> light() const { return light_; }
  std::optional<bool> locked() const { return lock_; }
  std::optional<bool> obstructed() const { return obstruction_; }
  const Secplus2RxStats &stats() const { return stats_; }
  size_t partial_size() const { return used_; }
#ifdef USE_OPENGARAGE_SECPLUS2_SYNC
  void set_own_client(uint32_t client) { own_client_ = client; }
  uint32_t last_byte_ms() const { return last_byte_ms_; }
  std::optional<uint16_t> openings() const { return openings_; }
  uint32_t openings_frames() const { return openings_frames_; }
#endif

 protected:
  bool status_link_seen_{false};
  uint32_t last_status_link_ms_{0};
  static void increment_(uint32_t &value) { if (value != UINT32_MAX) ++value; }
  static DoorState decode_door_(uint8_t value) {
    switch (value) {
      case 1: return DoorState::OPEN;
      case 2: return DoorState::CLOSED;
      case 3: return DoorState::STOPPED;
      case 4: return DoorState::OPENING;
      case 5: return DoorState::CLOSING;
      default: return DoorState::UNKNOWN;
    }
  }
  void invalidate_status_() {
    status_seen_ = false;
    door_ = DoorState::UNKNOWN;
    light_.reset();
    lock_.reset();
    obstruction_.reset();
  }
  void resync_() {
    // Failed candidate: retain the next complete prefix, otherwise only a suffix
    // that could become a prefix. All scans/copies are bounded by nineteen bytes.
    for (size_t i = 1; i + 2 < PACKET_SIZE; ++i) {
      if (packet_[i] == 0x55 && packet_[i + 1] == 1 && packet_[i + 2] == 0) {
        used_ = PACKET_SIZE - i;
        for (size_t j = 0; j < used_; ++j) packet_[j] = packet_[j + i];
        return;
      }
    }
    used_ = packet_[17] == 0x55 && packet_[18] == 1 ? 2 : (packet_[18] == 0x55 ? 1 : 0);
    if (used_) packet_[0] = 0x55;
    if (used_ == 2) packet_[1] = 1;
  }
  std::array<uint8_t, PACKET_SIZE> packet_{};
  size_t used_{0};
  uint32_t last_byte_ms_{0}, last_status_ms_{0}, status_timeout_ms_{360000};
  bool status_seen_{false};
  DoorState door_{DoorState::UNKNOWN};
  std::optional<bool> light_, lock_, obstruction_;
  Secplus2RxStats stats_;
#ifdef USE_OPENGARAGE_SECPLUS2_SYNC
  uint32_t own_client_{0}, openings_frames_{0};
  std::optional<uint16_t> openings_;
#endif
};

// Uart must provide available/read/overflow/discard_rx. This helper cannot write.
// The clock budget is checked between byte operations, not a hard IRQ/decoder WCET.
template<class Uart, class MicroClock>
bool pump_secplus2(Uart &uart, Secplus2Receiver &receiver, uint32_t now, MicroClock clock) {
  constexpr size_t BYTE_BUDGET = 64;
  constexpr uint32_t TIME_BUDGET_US = 1000;
  const uint32_t start = clock();
  receiver.tick(now);
  const int available = uart.available();  // SoftwareSerial drains its bounded ISR ring here.
  if (uart.overflow()) {
    receiver.note_overflow();
    uart.discard_rx();
    receiver.note_service(uint32_t(clock() - start));
    return false;
  }
  const size_t waiting = available > 0 ? size_t(available) : 0;
  receiver.note_depth(waiting);
  size_t consumed = 0;
  while (consumed < waiting && consumed < BYTE_BUDGET && uint32_t(clock() - start) < TIME_BUDGET_US) {
    const int byte = uart.read();
    if (byte < 0) break;
    receiver.feed(uint8_t(byte), now);
    ++consumed;
  }
  receiver.note_service(uint32_t(clock() - start));
  return consumed < waiting;  // Arrange another main-loop pass; no always-on busy loop.
}

}  // namespace esphome::opengarage
#endif
