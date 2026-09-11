// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/defines.h"
#ifdef USE_OPENGARAGE_SECPLUS2_RX
#include "secplus2_rx.h"
#include <SoftwareSerial.h>
#ifdef USE_OPENGARAGE_SECPLUS2_SYNC
#include "secplus2_sync.h"
#include "esphome/core/gpio.h"
#include <Ticker.h>
#endif

namespace esphome::opengarage {

class Secplus2Transport {
 public:
  void start(int8_t rx_pin);
  void loop(uint32_t now);
  void stop();
  bool started() const { return started_; }
  Secplus2Receiver &receiver() { return receiver_; }
#ifdef USE_OPENGARAGE_SECPLUS2_SYNC
  void start_queries(InternalGPIOPin *rx, InternalGPIOPin *tx, uint32_t client, uint32_t now);
  const Secplus2QuerySession &session() const { return session_; }
  uint32_t query_writes() const { return query_writes_; }
  uint32_t collisions() const { return collisions_; }
  uint32_t deferrals() const { return deferrals_; }
  uint32_t tx_errors() const { return tx_errors_; }
  uint32_t max_tx_us() const { return max_tx_us_; }
#ifdef USE_OPENGARAGE_SECPLUS2_CONTROL
  bool controls_available() const {
    return started_ && tx_ && !stopped_ && !control_fault_ &&
        session_.state() == Secplus2QuerySession::State::OBSERVED && receiver_.status_link_fresh();
  }
  bool command_idle(uint32_t now) const;
  bool press_door(uint32_t now);
  bool move_door(uint32_t now, bool open, DoorState expected);
  // UNKNOWN expected: caller completed the full warning; no position binding.
  bool toggle_door(uint32_t now, DoorState expected);
  bool set_light(uint32_t now, bool on);
  bool set_lock(uint32_t now, bool locked);
  bool releasing() const { return release_pending_; }
  void cancel_press() { expedited_release_ = release_pending_; }
  uint32_t door_commands() const { return door_commands_; }
  uint32_t light_commands() const { return light_commands_; }
  uint32_t lock_commands() const { return lock_commands_; }
#endif
#endif
 protected:
  class ReceiveUart : public SoftwareSerial {
   public:
    void discard_rx() {
      // Stop ISR production, decode/discard its remaining edges, flush decoded
      // bytes, and reset bit assembly when RX is enabled again. No allocation/TX.
      enableRx(false);
      (void) available();
      flush();
      (void) overflow();
      enableRx(true);
    }
  } uart_;
  Secplus2Receiver receiver_;
  bool started_{false};
#ifdef USE_OPENGARAGE_SECPLUS2_SYNC
  void service_queries_(uint32_t now, bool backlog);
  InternalGPIOPin *rx_{nullptr}, *tx_{nullptr};
  Secplus2QuerySession session_;
  Ticker force_low_;
  uint32_t query_writes_{0}, collisions_{0}, deferrals_{0}, tx_errors_{0}, max_tx_us_{0};
  bool stopped_{false};
#ifdef USE_OPENGARAGE_SECPLUS2_CONTROL
  enum class WriteResult { COLLISION, SENT, FAILED };
  bool door_press_(uint32_t now, bool toggle, DoorState expected, bool open);
  bool encode_release_(uint8_t *packet) const;
  WriteResult write_control_(const uint8_t *packet, bool force_release = false);
  void service_release_(uint32_t now, bool backlog);
  void emergency_release_();
  bool bus_idle_(uint32_t now) const;
  uint32_t pressed_ms_{0}, last_tx_ms_{0}, release_attempt_ms_{0};
  uint32_t door_commands_{0}, light_commands_{0}, lock_commands_{0};
  bool release_pending_{false}, release_open_{false}, expedited_release_{false};
  bool release_toggle_{false};
  bool control_fault_{false}, tx_seen_{false}, refresh_status_{false};
#endif
#endif
};

}  // namespace esphome::opengarage
#endif
