// SPDX-License-Identifier: GPL-3.0-or-later
#include "distance_sensor.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

namespace esphome::opengarage {

void IRAM_ATTR EchoCapture::gpio_intr(EchoCapture *capture) {
  const uint32_t now = micros();
  if (!capture->armed || capture->done) return;
  if (uint32_t(now - capture->trigger_us) >= DistanceFilter::ECHO_TIMEOUT_US) {
    capture->width_us = 0;
    capture->done = true;
    capture->armed = false;
    return;
  }
  if (capture->pin.digital_read()) {
    if (!capture->rising) {
      capture->rise_us = now;
      capture->rising = true;
    }
  } else if (capture->rising) {
    capture->width_us = now - capture->rise_us;
    capture->done = true;
    capture->armed = false;
  }
}

void DistanceSensor::setup() {
  trigger_->digital_write(false);
  trigger_->setup();
  echo_->setup();
  capture_.pin = echo_->to_isr();
  echo_->attach_interrupt(EchoCapture::gpio_intr, &capture_, gpio::INTERRUPT_ANY_EDGE);
}

void DistanceSensor::loop(uint32_t now_ms) {
  if (pending_) {
    bool complete = false;
    uint32_t width = 0;
    {
      InterruptLock lock;
      if (capture_.done) {
        width = capture_.width_us;
        complete = true;
      } else if (uint32_t(micros() - capture_.trigger_us) >= DistanceFilter::ECHO_TIMEOUT_US) {
        complete = true;
      }
      if (complete) capture_.armed = false;
    }
    if (complete) {
      pending_ = false;
      // Date the sample at acquisition, not when a delayed loop consumes it.
      filter_.push(width, last_trigger_ms_);
    }
  }
  if (pending_ || (started_ && uint32_t(now_ms - last_trigger_ms_) < interval_ms_)) return;
  // Only the 22 us trigger pulse is synchronous. Echo waiting is interrupt driven.
  {
    InterruptLock lock;
    capture_.armed = false;
    trigger_->digital_write(false);
    delayMicroseconds(2);
    capture_.rising = capture_.done = false;
    capture_.trigger_us = micros();
    capture_.armed = true;
    trigger_->digital_write(true);
    delayMicroseconds(20);
    trigger_->digital_write(false);
  }
  started_ = pending_ = true;
  last_trigger_ms_ = now_ms;
}

void DistanceSensor::shutdown() {
  {
    InterruptLock lock;
    capture_.armed = false;
  }
  echo_->detach_interrupt();
  trigger_->digital_write(false);
  pending_ = false;
}

}  // namespace esphome::opengarage
