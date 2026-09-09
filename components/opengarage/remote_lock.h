// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/defines.h"
#if defined(USE_OPENGARAGE_SECPLUS1_CONTROL) || defined(USE_OPENGARAGE_SECPLUS2_CONTROL)
#include "esphome/components/lock/lock.h"
#include <optional>

namespace esphome::opengarage {
// One short-lived intent, never a retry queue. Each backend owns its wire format,
// final RX drain, release cleanup and (Sec+2) shared rolling sequence.
template<typename Transport> class RemoteLockIntent {
 public:
  explicit RemoteLockIntent(Transport &port) : port_(port) {}
  bool request(uint32_t now, bool target, bool enabled) {
    port_.receiver().tick(now);
    if (!enabled || !port_.controls_available()) return reject_("Controls unavailable");
    if (waiting_ || port_.releasing() || (cooldown_ && uint32_t(now - sent_ms_) < 2000))
      return reject_("Busy; not queued");
    const auto value = port_.receiver().locked();
    if (!value) return reject_("Lock state unknown");
    if (*value == target) return reject_("Already at requested state; no command");
    target_ = target; requested_ms_ = now; waiting_ = true;
    reason_ = "Waiting for bus idle";
    return true;
  }
  void tick(uint32_t now, bool enabled) {
    port_.receiver().tick(now);
    if (!waiting_) return;
    if (!enabled || !port_.controls_available()) { cancel(); return; }
    const auto value = port_.receiver().locked();
    if (!value) { waiting_ = false; reason_ = "Lock state unknown; canceled"; return; }
    if (*value == target_) { waiting_ = false; reason_ = "Target observed; no command"; return; }
    if (uint32_t(now - requested_ms_) >= 1000) { waiting_ = false; reason_ = "Bus wait expired; no command"; return; }
    if (!port_.command_idle(now)) return;
    waiting_ = false;
    if (port_.set_lock(now, target_)) {
      sent_ms_ = now; cooldown_ = true; reason_ = "Lock command sent; awaiting reported state";
    } else reason_ = "Lock write refused or failed; not retried";
  }
  void cancel() { waiting_ = false; reason_ = "Canceled; no pending lock command"; }
  bool waiting() const { return waiting_; }
  const char *reason() const { return reason_; }
 protected:
  bool reject_(const char *reason) { reason_ = reason; return false; }
  Transport &port_;
  uint32_t requested_ms_{0}, sent_ms_{0};
  bool waiting_{false}, target_{false}, cooldown_{false};
  const char *reason_{"No lock command"};
};

class RemoteLockCommands {
 public:
  virtual void request_lock(bool locked) = 0;
};

// Remote-control lockout, not a mechanical deadbolt or an OG command inhibit.
// Unlike Cover, ESPHome Lock has an explicit NONE/unknown state in its API enum.
class RemoteLock : public lock::Lock {
 public:
  explicit RemoteLock(RemoteLockCommands *parent) : parent_(parent) {
    traits.set_supports_open(false);
    traits.set_requires_code(false);
    traits.set_assumed_state(false);
    traits.set_supported_states({lock::LOCK_STATE_NONE, lock::LOCK_STATE_LOCKED, lock::LOCK_STATE_UNLOCKED});
  }
  void observe(std::optional<bool> value) {
    const auto next = !value ? lock::LOCK_STATE_NONE :
        *value ? lock::LOCK_STATE_LOCKED : lock::LOCK_STATE_UNLOCKED;
    if (!published_ || state != next) { published_ = true; publish_state(next); }
  }
 protected:
  void control(const lock::LockCall &call) override {
    const auto target = call.get_state();
    if (!target) return;
    if (*target == lock::LOCK_STATE_LOCKED) parent_->request_lock(true);
    else if (*target == lock::LOCK_STATE_UNLOCKED) parent_->request_lock(false);
    // No optimistic publication, startup/restore action, unlatch or raw-state command.
  }
  RemoteLockCommands *parent_;
  bool published_{false};
};
}  // namespace esphome::opengarage
#endif
