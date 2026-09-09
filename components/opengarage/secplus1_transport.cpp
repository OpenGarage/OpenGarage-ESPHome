// SPDX-License-Identifier: GPL-3.0-or-later
#include "secplus1_transport.h"
#ifdef USE_OPENGARAGE_SECPLUS1
#include "esphome/core/application.h"
#include "esphome/core/hal.h"

namespace esphome::opengarage {
static void IRAM_ATTR secplus1_rx_wake() {
  // A wake-up is not a decoded frame or proof of an occupied bus. Requiring
  // 50 ms without callbacks here starved panel initialization under RX activity.
  App.wake_loop_threadsafe();
}
void Secplus1Transport::start(InternalGPIOPin *rx, InternalGPIOPin *tx, uint32_t now, bool emulate_if_needed) {
  if (started_ || !rx || rx->get_pin() != 5 || (tx && tx->get_pin() != 15)) return;
  rx_ = rx; tx_ = tx;
  if (tx_) { tx_->digital_write(false); tx_->pin_mode(gpio::FLAG_OUTPUT); }
  uart_.onReceive(secplus1_rx_wake);
  // v2.3+ Q3 pulls GPIO5 LOW only; no external pull-up is fitted. Match stock
  // SoftwareSerial's INPUT_PULLUP so Q3 turning off produces a valid HIGH.
  uart_.enableRxGPIOPullUp(true);
  uart_.begin(1200, SWSERIAL_8E1, 5, tx_ ? 15 : -1, true, 128, 1280);
  started_ = uart_.isListening();
  if (started_) rx_high_ = rx_->digital_read();
  if (started_) panel_.start(tx_ != nullptr, now, emulate_if_needed);
}
void Secplus1Transport::loop(uint32_t now) {
  if (!started_) return;
  const bool backlog = pump_secplus1(uart_, receiver_, now, [] { return micros(); });
  if (backlog) App.wake_loop_threadsafe();
  panel_.tick(now, receiver_.observed_status(), receiver_.panel37_seen());
  rx_high_ = rx_->digital_read();  // Main-loop snapshot, not continuous level measurement.
#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
  // A press has already been sent; its release tail outranks all polling and
  // completes even if door state, link, or a local cancellation changes.
  if (releases_left_) { service_release_(now, backlog); return; }
  if (control_fault_) return;
#endif
  const auto next = panel_.due(now);
  if (!next) return;
  // Wait for idle: drain before sending, avoid partial responses and a physically
  // active RX line. Not collision-proof if another sender starts simultaneously.
  // Keep byte/frame/level guards, but do not turn raw RX wake-ups into a
  // 50 ms transmit veto. Stock panel emulation has no such callback-age guard.
  // Record every simultaneous remaining reason; any bit prevents the write.
  last_block_mask_ = (backlog ? SEC1_BACKLOG : 0) | (receiver_.partial() ? SEC1_PARTIAL : 0) |
      (uint32_t(now - receiver_.last_byte_ms()) < 50 ? SEC1_RECENT_BYTE : 0) |
      (rx_high_ ? SEC1_RX_HIGH : 0);
  tx_attempt_seen_ = true;
  if (last_block_mask_) {
    receiver_.note_deferred(last_block_mask_); return;
  }
  const uint32_t start = micros();
  // One 1200/8E1 byte is ~9.2 ms. SoftwareSerial keeps interrupts enabled by
  // default; never loop over a complete initialization sequence in one service.
  const bool ok = uart_.write(*next) == 1;
#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
  last_tx_ms_ = now; tx_seen_ = true;
#endif
  receiver_.note_tx(ok);
  receiver_.note_service(uint32_t(micros() - start));
  panel_.sent(now, ok);
  if (!ok && tx_) tx_->digital_write(false);
}
void Secplus1Transport::stop() {
#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
  emergency_release_();
#endif
  panel_.stop();
  if (started_) { uart_.end(); started_ = false; }
  if (tx_) tx_->digital_write(false);
  receiver_.transport_loss();
}
#ifdef USE_OPENGARAGE_SECPLUS1_CONTROL
bool Secplus1Transport::command_idle(uint32_t now) const {
  return controls_available() && !releases_left_ && !receiver_.partial() &&
      (!tx_seen_ || uint32_t(now - last_tx_ms_) >= 50) &&
      uint32_t(now - receiver_.last_byte_ms()) >= 50 && !rx_->digital_read();
}
bool Secplus1Transport::press_door(uint32_t now) {
  return door_press_(now, false);
}
bool Secplus1Transport::toggle_door(uint32_t now, DoorState expected) {
  return door_press_(now, true, expected);
}
bool Secplus1Transport::door_press_(uint32_t now, bool toggle, DoorState expected) {
  receiver_.tick(now);
  const auto door = receiver_.door();
  if (door == DoorState::UNKNOWN || (!toggle && door != DoorState::CLOSED && door != DoorState::OPEN)) return false;
  if (toggle && door != expected) return false; // Bind the controller's warning/motion decision to this report.
  if (!press_(0x30, 0x31, now, door, {})) return false;
  ++door_commands_;
  return true;
}
bool Secplus1Transport::press_light(uint32_t now) {
  receiver_.tick(now);
  const auto light = receiver_.light();
  if (!light.has_value() || !press_(0x32, 0x33, now, DoorState::UNKNOWN, light)) return false;
  ++light_commands_;
  return true;
}
bool Secplus1Transport::set_lock(uint32_t now, bool locked) {
  receiver_.tick(now);
  const auto baseline = receiver_.locked();
  if (!baseline || *baseline == locked || !press_(0x34, 0x35, now, DoorState::UNKNOWN, baseline)) return false;
  ++lock_commands_;
  return true; // Stock lock-button toggle, only from a fresh differing state.
}
bool Secplus1Transport::press_(uint8_t press, uint8_t release, uint32_t now,
                              DoorState door, std::optional<bool> binary) {
  if (!command_idle(now)) return false;
  // Drain once more immediately before a new press, including the UART edge ring.
  const bool backlog = pump_secplus1(uart_, receiver_, now, [] { return micros(); });
  if (backlog || !command_idle(now)) return false;
  if ((press == 0x30 && receiver_.door() != door) ||
      (press == 0x32 && receiver_.light() != binary) ||
      (press == 0x34 && receiver_.locked() != binary)) return false;
  release_byte_ = release;
  releases_left_ = 2;
  pressed_ms_ = release_ms_ = now;
  expedited_release_ = false;
  // Independent electrical force-LOW only; never write UART/tone/network in a timer.
  // A protocol release still requires main-loop service or lifecycle teardown.
  force_low_.once_ms(1000, [this]() { tx_->digital_write(false); });
  const auto begin = micros();
  const bool ok = uart_.write(press) == 1;
  last_tx_ms_ = now; tx_seen_ = true;
  receiver_.note_tx(ok);
  receiver_.note_service(uint32_t(micros() - begin));
  panel_.defer_poll(now);
  if (!ok) { control_fault_ = true; expedited_release_ = true; }
  return ok;  // Software write only, not an opener acknowledgement.
}
void Secplus1Transport::service_release_(uint32_t now, bool backlog) {
  const bool overdue = uint32_t(now - pressed_ms_) >= 1000;
  const uint32_t delay_ms = releases_left_ == 2 ? 250 : 40;
  if (!overdue && !(expedited_release_ && releases_left_ == 2) &&
      uint32_t(now - release_ms_) < delay_ms) return;
  // Best-effort idle avoidance. At the deadline prioritize releasing a button
  // already pressed; do not hold it forever waiting for a quiet bus.
  if (!overdue && (backlog || receiver_.partial() ||
      uint32_t(now - receiver_.last_byte_ms()) < 50 || rx_->digital_read())) return;
  const auto begin = micros();
  const bool ok = uart_.write(release_byte_) == 1;
  last_tx_ms_ = now; tx_seen_ = true;
  receiver_.note_tx(ok);
  receiver_.note_service(uint32_t(micros() - begin));
  if (!ok) control_fault_ = true;
  --releases_left_; release_ms_ = now; panel_.defer_poll(now);
  if (!releases_left_) { force_low_.detach(); tx_->digital_write(false); }
}
void Secplus1Transport::emergency_release_() {
  if (started_ && tx_ && releases_left_) {
    // Shutdown/OTA may not service another loop. Two bounded release-only
    // writes (~18.4 ms total) replace normal 250/40 ms scheduling here.
    receiver_.note_tx(uart_.write(release_byte_) == 1);
    receiver_.note_tx(uart_.write(release_byte_) == 1);
  }
  releases_left_ = 0;
  force_low_.detach();
}
#endif
void Secplus1Transport::format_tx_block(char *out, size_t capacity) const {
  if (!capacity) return;
  if (!started_ || panel_.state() != Secplus1PanelState::EMULATING) {
    std::snprintf(out, capacity, "Polling inactive");
  } else if (!tx_attempt_seen_) {
    std::snprintf(out, capacity, "No due attempt yet");
  } else if (!last_block_mask_) {
    std::snprintf(out, capacity, "None at last attempt");
  } else {
    std::snprintf(out, capacity, "%s%s%s%s",
        last_block_mask_ & SEC1_BACKLOG ? "backlog " : "",
        last_block_mask_ & SEC1_PARTIAL ? "partial " : "",
        last_block_mask_ & SEC1_RECENT_BYTE ? "recent-byte " : "",
        last_block_mask_ & SEC1_RX_HIGH ? "RX-high" : "");
  }
}
} // namespace esphome::opengarage
#endif
