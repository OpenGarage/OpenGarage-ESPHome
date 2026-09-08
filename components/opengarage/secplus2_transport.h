// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/defines.h"
#ifdef USE_OPENGARAGE_SECPLUS2_RX
#include "secplus2_rx.h"
#include <SoftwareSerial.h>

namespace esphome::opengarage {

class Secplus2Transport {
 public:
  void start(int8_t rx_pin);
  void loop(uint32_t now);
  void stop();
  bool started() const { return started_; }
  Secplus2Receiver &receiver() { return receiver_; }
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
};

}  // namespace esphome::opengarage
#endif
