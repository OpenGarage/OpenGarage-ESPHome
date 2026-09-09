// SPDX-License-Identifier: GPL-3.0-or-later
#include "secplus2_transport.h"
#ifdef USE_OPENGARAGE_SECPLUS2_RX
#include "esphome/core/application.h"
#include "esphome/core/hal.h"

namespace esphome::opengarage {
// Callback runs from SoftwareSerial's ISR: wake only, no logging/decoding/allocation.
static void IRAM_ATTR secplus2_rx_wake() { App.wake_loop_threadsafe(); }

void Secplus2Transport::start(int8_t rx_pin) {
  if (started_ || rx_pin != 5) return;
  // Fresh UART object has TX=-1; no beginTx(), preamble, write(), or GPIO15 claim.
  uart_.onReceive(secplus2_rx_wake);
  // v2.3+ Q3 is an open-drain RX stage with no external GPIO5 pull-up.
  // Enable the internal pull-up before begin(), matching stock SoftwareSerial.
  uart_.enableRxGPIOPullUp(true);
  uart_.begin(9600, SWSERIAL_8N1, rx_pin, -1, true, 128, 1280);
  started_ = uart_.isListening();
}

void Secplus2Transport::loop(uint32_t now) {
  if (!started_) return;
  const bool backlog = pump_secplus2(uart_, receiver_, now, []() { return micros(); });
  if (backlog) App.wake_loop_threadsafe();
#ifdef USE_OPENGARAGE_SECPLUS2_CONTROL
  // Only a release tail can remain after a dispatched press. It outranks queries
  // and survives link/state/cancel changes; there is no queued future press.
  if (release_pending_) { service_release_(now, backlog); return; }
  if (control_fault_) return;
#endif
#ifdef USE_OPENGARAGE_SECPLUS2_SYNC
  if (tx_) service_queries_(now, backlog);
#endif
}

void Secplus2Transport::stop() {
#ifdef USE_OPENGARAGE_SECPLUS2_CONTROL
  emergency_release_();
#endif
#ifdef USE_OPENGARAGE_SECPLUS2_SYNC
  stopped_ = true;
  session_.stop();
  force_low_.detach();
#endif
  if (started_) { uart_.end(); started_ = false; }
#ifdef USE_OPENGARAGE_SECPLUS2_SYNC
  if (tx_) tx_->digital_write(false);
#endif
  receiver_.transport_loss();
}
#ifdef USE_OPENGARAGE_SECPLUS2_SYNC
void Secplus2Transport::start_queries(InternalGPIOPin *rx, InternalGPIOPin *tx, uint32_t client, uint32_t now) {
  if (started_ || stopped_ || !rx || !tx || rx->get_pin() != 5 || tx->get_pin() != 15 || !client) return;
  rx_ = rx; tx_ = tx;
  tx_->digital_write(false); // Set inactive latch before OUTPUT, compatible with boot pulldown.
  tx_->pin_mode(gpio::FLAG_OUTPUT);
  uart_.onReceive(secplus2_rx_wake);
  uart_.enableRxGPIOPullUp(true);
  uart_.begin(9600, SWSERIAL_8N1, 5, 15, true, 128, 1280);
  uart_.enableIntTx(false); // Match canonical stock full-frame timing (~20 ms blocking).
  started_ = uart_.isListening();
  if (started_) { receiver_.set_own_client(client); session_.start(client, now); }
}

void Secplus2Transport::service_queries_(uint32_t now, bool backlog) {
  session_.tick(now, receiver_);
  auto command = session_.due(now);
#ifdef USE_OPENGARAGE_SECPLUS2_CONTROL
  // One status refresh following control, not a command retry or acknowledgement.
  if (refresh_status_ && session_.state() == Secplus2QuerySession::State::OBSERVED &&
      uint32_t(now - last_tx_ms_) >= 100) command = Secplus2QuerySession::GET_STATUS;
#endif
  if (!command) return;
#ifdef USE_OPENGARAGE_SECPLUS2_CONTROL
  refresh_status_ = false; // A busy/colliding refresh falls back to normal query pacing.
  if (tx_seen_ && uint32_t(now - last_tx_ms_) < 100) return;
#endif
  // Raw RX wake-up age is deliberately NOT an idle veto (Sec+ 1.0 lesson).
  if (backlog || receiver_.partial_size() || uint32_t(now - receiver_.last_byte_ms()) < 20 || rx_->digital_read()) {
    ++deferrals_; session_.defer(now); return;
  }
  uint8_t packet[Secplus2Receiver::PACKET_SIZE];
  if (!session_.encode_query(*command, packet)) {
    ++tx_errors_; session_.sent(*command, now, false, receiver_); return;
  }
  const uint32_t begin = micros();
  // SDK timer is an electrical force-LOW backstop, not a protocol/timing guarantee.
  force_low_.once_ms(100, [this] { tx_->digital_write(false); });
  tx_->digital_write(true);
  delayMicroseconds(1300);
  tx_->digital_write(false);
  delayMicroseconds(130);
  if (rx_->digital_read()) {
    ++collisions_; force_low_.detach(); session_.defer(now); return;
  }
  const bool ok = uart_.write(packet, sizeof(packet)) == sizeof(packet);
  delayMicroseconds(100);
  tx_->digital_write(false);
  force_low_.detach();
  max_tx_us_ = std::max(max_tx_us_, uint32_t(micros() - begin));
  if (ok) ++query_writes_; else ++tx_errors_;
#ifdef USE_OPENGARAGE_SECPLUS2_CONTROL
  last_tx_ms_ = millis(); tx_seen_ = true;
#endif
  session_.sent(*command, millis(), ok, receiver_);
  // Parsing resumes next pass; do not synthesize status from our transmitted data.
}
#ifdef USE_OPENGARAGE_SECPLUS2_CONTROL
bool Secplus2Transport::bus_idle_(uint32_t now) const {
  return !receiver_.partial_size() && uint32_t(now - receiver_.last_byte_ms()) >= 20 &&
      (!tx_seen_ || uint32_t(now - last_tx_ms_) >= 100) && !rx_->digital_read();
}
bool Secplus2Transport::command_idle(uint32_t now) const {
  return controls_available() && !release_pending_ && bus_idle_(now);
}
Secplus2Transport::WriteResult Secplus2Transport::write_control_(const uint8_t *packet, bool force_release) {
  const uint32_t begin = micros();
  force_low_.once_ms(100, [this] { tx_->digital_write(false); });
  tx_->digital_write(true);
  delayMicroseconds(1300);
  tx_->digital_write(false);
  delayMicroseconds(130);
  if (!force_release && rx_->digital_read()) {
    ++collisions_; force_low_.detach(); return WriteResult::COLLISION;
  }
  const bool ok = uart_.write(packet, Secplus2Receiver::PACKET_SIZE) == Secplus2Receiver::PACKET_SIZE;
  delayMicroseconds(100);
  tx_->digital_write(false); force_low_.detach();
  max_tx_us_ = std::max(max_tx_us_, uint32_t(micros() - begin));
  last_tx_ms_ = millis(); tx_seen_ = true;
  session_.control_sent(last_tx_ms_, ok); // Shared query/control counter, including partial writes.
  if (!ok) { ++tx_errors_; control_fault_ = true; }
  return ok ? WriteResult::SENT : WriteResult::FAILED;
}
bool Secplus2Transport::press_door(uint32_t now) {
  receiver_.tick(now);
  const auto baseline = receiver_.door();
  if ((baseline != DoorState::OPEN && baseline != DoorState::CLOSED) || !command_idle(now)) return false;
  // Decode any pending edges/bytes before choosing a direction. Refuse if the
  // endpoint changed, even if the controller hasn't published it yet.
  const bool backlog = pump_secplus2(uart_, receiver_, now, [] { return micros(); });
  if (backlog || !command_idle(now) || receiver_.door() != baseline) return false;
  uint8_t packet[Secplus2Receiver::PACKET_SIZE];
  const bool open = baseline == DoorState::CLOSED;
  if (!session_.encode_door(open, true, packet)) return false;
  const auto result = write_control_(packet);
  if (result == WriteResult::COLLISION) return false; // No UART write, no late retry.
  release_open_ = open;
  release_pending_ = true;
  pressed_ms_ = release_attempt_ms_ = millis();
  expedited_release_ = result == WriteResult::FAILED;
  if (result != WriteResult::SENT) return false; // Partial press is ambiguous; release only.
  ++door_commands_;
  return true; // Software write, not an opener acknowledgement.
}
bool Secplus2Transport::set_light(uint32_t now, bool on) {
  receiver_.tick(now);
  if (!command_idle(now)) return false;
  const bool backlog = pump_secplus2(uart_, receiver_, now, [] { return micros(); });
  const auto value = receiver_.light();
  if (backlog || !command_idle(now) || !value || *value == on) return false;
  uint8_t packet[Secplus2Receiver::PACKET_SIZE];
  if (!session_.encode_light(on, packet) || write_control_(packet) != WriteResult::SENT) return false;
  ++light_commands_; refresh_status_ = true;
  return true; // Explicit ON/OFF; never toggle, synthesize observed state or retry.
}
bool Secplus2Transport::set_lock(uint32_t now, bool locked) {
  receiver_.tick(now);
  if (!command_idle(now)) return false;
  const bool backlog = pump_secplus2(uart_, receiver_, now, [] { return micros(); });
  const auto value = receiver_.locked();
  if (backlog || !command_idle(now) || !value || *value == locked) return false;
  uint8_t packet[Secplus2Receiver::PACKET_SIZE];
  if (!session_.encode_lock(locked, packet) || write_control_(packet) != WriteResult::SENT) return false;
  ++lock_commands_; refresh_status_ = true;
  return true; // Explicit LOCK/UNLOCK, never a guessed toggle or acknowledgement.
}
void Secplus2Transport::service_release_(uint32_t now, bool backlog) {
  const bool overdue = uint32_t(now - pressed_ms_) >= 1000;
  if (!overdue && ((!expedited_release_ && uint32_t(now - pressed_ms_) < 250) ||
      uint32_t(now - release_attempt_ms_) < 50)) return;
  if (!overdue && (backlog || !bus_idle_(now))) return;
  release_attempt_ms_ = now;
  uint8_t packet[Secplus2Receiver::PACKET_SIZE];
  if (!session_.encode_door(release_open_, false, packet)) {
    control_fault_ = true; release_pending_ = false; return;
  }
  const auto result = write_control_(packet, overdue);
  if (result == WriteResult::COLLISION) return; // Release-only idle retries, at most every 50 ms.
  release_pending_ = false; refresh_status_ = result == WriteResult::SENT;
  // One bounded best-effort release even on a stuck-busy bus. UART/timers cannot
  // guarantee delivery during system failure; never claim electrical LOW is a protocol release.
  if (overdue) control_fault_ = true; // Require reboot after abnormal cleanup.
}
void Secplus2Transport::emergency_release_() {
  if (started_ && tx_ && release_pending_) {
    uint8_t packet[Secplus2Receiver::PACKET_SIZE];
    if (session_.encode_door(release_open_, false, packet)) (void) write_control_(packet, true);
  }
  release_pending_ = false;
  force_low_.detach();
}
#endif
#endif
}  // namespace esphome::opengarage
#endif
