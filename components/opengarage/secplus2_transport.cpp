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
#ifdef USE_OPENGARAGE_SECPLUS2_SYNC
  if (tx_) service_queries_(now, backlog);
#endif
}

void Secplus2Transport::stop() {
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
  const auto command = session_.due(now);
  if (!command) return;
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
  session_.sent(*command, millis(), ok, receiver_);
  // Parsing resumes next pass; do not synthesize status from our transmitted data.
}
#endif
}  // namespace esphome::opengarage
#endif
