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
  if (pump_secplus2(uart_, receiver_, now, []() { return micros(); })) App.wake_loop_threadsafe();
}

void Secplus2Transport::stop() {
  if (started_) { uart_.end(); started_ = false; }
  receiver_.transport_loss();
}
}  // namespace esphome::opengarage
#endif
