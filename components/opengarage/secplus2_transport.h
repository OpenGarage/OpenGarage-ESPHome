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
#endif
};

}  // namespace esphome::opengarage
#endif
