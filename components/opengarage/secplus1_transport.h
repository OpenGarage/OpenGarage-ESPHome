// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/defines.h"
#ifdef USE_OPENGARAGE_SECPLUS1
#include "secplus1_rx.h"
#include "esphome/core/gpio.h"
#include <SoftwareSerial.h>
#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
#include <Ticker.h>
#endif

namespace esphome::opengarage {
class Secplus1Transport {
 public:
  void start(InternalGPIOPin *rx, InternalGPIOPin *tx, uint32_t now);
  void loop(uint32_t now);
  void stop();
  bool started() const { return started_; }
  Secplus1Receiver &receiver() { return receiver_; }
  Secplus1PanelState panel_state() const { return panel_.state(); }
  std::optional<bool> rx_high() const { return started_ ? std::optional<bool>(rx_high_) : std::nullopt; }
  uint8_t last_block_mask() const { return last_block_mask_; }
  // Last due-attempt result, or explicit no-attempt/inactive state; not a live interlock API.
  void format_tx_block(char *out, size_t capacity) const;
#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
  bool controls_available() const { return started_ && tx_ && !control_fault_ && panel_.commands_ready(); }
  bool command_idle(uint32_t now) const;
  bool press_door(uint32_t now);
  bool press_light(uint32_t now);
  bool releasing() const { return releases_left_ != 0; }
  // After a press has gone out, cancel can only expedite its release, never undo it.
  void cancel_press() { expedited_release_ = releases_left_ != 0; }
  uint32_t door_commands() const { return door_commands_; }
  uint32_t light_commands() const { return light_commands_; }
 protected:
  bool press_(uint8_t press, uint8_t release, uint32_t now, DoorState door, std::optional<bool> light);
  void service_release_(uint32_t now, bool backlog);
  void emergency_release_();
  Ticker force_low_;
  uint32_t pressed_ms_{0}, release_ms_{0}, last_tx_ms_{0}, door_commands_{0}, light_commands_{0};
  uint8_t release_byte_{0}, releases_left_{0};
  bool expedited_release_{false}, control_fault_{false}, tx_seen_{false};
#endif
 protected:
  class ReceiveUart : public SoftwareSerial {
   public:
    void discard_rx() { enableRx(false); (void)available(); flush(); (void)overflow(); enableRx(true); }
  } uart_;
  Secplus1Receiver receiver_;
  Secplus1Panel panel_;
  InternalGPIOPin *rx_{nullptr}, *tx_{nullptr};
  bool started_{false};
  bool rx_high_{false}, tx_attempt_seen_{false};
  uint8_t last_block_mask_{0};
};
} // namespace esphome::opengarage
#endif
