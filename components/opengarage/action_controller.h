// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "state_resolver.h"
#include <cstdint>

namespace esphome::opengarage {

// The initial M2 bench surface is toggle + cancel, not guessed open/close/stop.
class ControlOutputs {
 public:
  virtual ~ControlOutputs() = default;
  virtual void warning_start(uint32_t minimum_ms) = 0;
  virtual bool warning_tick(uint32_t elapsed_ms) = 0;
  virtual void warning_stop() = 0;
  virtual bool pulse(uint32_t duration_ms) = 0;
  // A wall-button press is distinct from an endpoint-directed cover request.
  virtual bool toggle(uint32_t duration_ms, DoorState) { return pulse(duration_ms); }
  // Pulse/Sec+ 1.0 retain endpoint-only behavior. Sec+ 2.0 carries the
  // explicit target and the observed state into its final pre-write check.
  virtual bool directed(uint32_t duration_ms, bool, DoorState) { return pulse(duration_ms); }
  virtual bool supports_stopped_direction() const { return false; }
  virtual bool reports_motion() const { return false; }
  virtual bool pulse_active() const = 0;
  virtual void stop() = 0;
  // Readiness loss still stops actuation/warnings, but need not stop diagnostics.
  virtual void stop_for_readiness() { stop(); }
};

enum class ActionPhase : uint8_t { DISARMED, IDLE, WARNING, PULSING, LOCKOUT };
enum class DoorCommand : uint8_t { TOGGLE, OPEN, CLOSE };
enum class CommandSource : uint8_t { NETWORK, LOCAL_BUTTON };
enum class ActionReason : uint8_t {
  NONE, DISARMED, ARMED, WARNING, DISPATCHED, BUSY, LOCKOUT, UNKNOWN_STATE,
  HARDWARE_MISMATCH, LINK_DOWN, CANCELED, STATE_CHANGED, LOOP_STALL, EXPIRED,
  SHUTDOWN, OUTPUT_FAILURE, CONFIG_INVALID, READY, ALREADY_AT_TARGET, UNSUPPORTED,
  REPEAT_GUARD, TARGET_LOCKOUT
};

inline const char *action_phase_name(ActionPhase phase) {
  switch (phase) {
    case ActionPhase::IDLE: return "Armed idle";
    case ActionPhase::WARNING: return "Buzzer warning";
    case ActionPhase::PULSING: return "Pulsing";
    case ActionPhase::LOCKOUT: return "Lockout";
    default: return "Disarmed";
  }
}
inline const char *action_reason_name(ActionReason reason) {
  switch (reason) {
    case ActionReason::NONE: return "None";
    case ActionReason::ARMED: return "Locally armed";
    case ActionReason::WARNING: return "Warning started";
    case ActionReason::DISPATCHED: return "Pulse dispatched";
    case ActionReason::BUSY: return "Busy; not queued";
    case ActionReason::LOCKOUT: return "Command lockout";
    case ActionReason::REPEAT_GUARD: return "Brief repeat guard; not queued";
    case ActionReason::TARGET_LOCKOUT: return "Open/Close cooldown; Toggle available";
    case ActionReason::UNKNOWN_STATE: return "Required state unknown";
    case ActionReason::HARDWARE_MISMATCH: return "Bench identity or hardware mismatch";
    case ActionReason::LINK_DOWN: return "Wi-Fi disconnected";
    case ActionReason::CANCELED: return "Canceled; no pending pulse";
    case ActionReason::STATE_CHANGED: return "Observed door state changed";
    case ActionReason::LOOP_STALL: return "Control loop stalled; disarmed";
    case ActionReason::EXPIRED: return "Arming session expired";
    case ActionReason::SHUTDOWN: return "OTA or shutdown; disarmed";
    case ActionReason::OUTPUT_FAILURE: return "Output failure; disarmed";
    case ActionReason::CONFIG_INVALID: return "Invalid timing configuration";
    case ActionReason::READY: return "Controls ready";
    case ActionReason::ALREADY_AT_TARGET: return "Already at requested state; no pulse";
    case ActionReason::UNSUPPORTED: return "Unsupported cover command; no pulse";
    default: return "Controls disarmed";
  }
}

struct ActionConfig {
  uint32_t warning_ms{5000};
  uint32_t pulse_ms{1000};  // Stock default, deliberately capped at 1 s for this bench slice.
  uint32_t lockout_ms{30000}; // Cover Open/Close cooldown; all commands in the historical bench profile.
  bool bench_mode{true};
};

class ActionController {
 public:
  static constexpr uint32_t MAX_SERVICE_GAP_MS = 500;
  static constexpr uint32_t ARM_SESSION_MS = 300000;
  static constexpr uint32_t REPEAT_GUARD_MS = 1000;
  explicit ActionController(ControlOutputs &outputs) : outputs_(outputs) {}
  bool configure(ActionConfig config) {
    config_valid_ = config.warning_ms >= 5000 && config.warning_ms <= 30000 &&
                    config.pulse_ms >= 100 && config.pulse_ms <= 1000 &&
                    config.lockout_ms >= 5000 && config.lockout_ms <= 120000;
    if (config_valid_) config_ = config;
    disarm(config_valid_ ? ActionReason::DISARMED : ActionReason::CONFIG_INVALID);
    return config_valid_;
  }
  void initialize(uint32_t now) {
    outputs_.stop();
    armed_ = cooldown_ = inputs_seen_ = false;
    dispatch_seen_ = false;
    stopped_ = manually_disabled_ = false;
    action_source_ = CommandSource::NETWORK;
    phase_ = ActionPhase::DISARMED;
    last_service_ = now;
    reason_ = config_valid_ ? ActionReason::DISARMED : ActionReason::CONFIG_INVALID;
  }
  void update(uint32_t now, DoorState state, bool hardware_ok, bool link_ok) {
    const uint32_t gap = uint32_t(now - last_service_);
    state_ = state;
    hardware_ok_ = hardware_ok;
    link_ok_ = link_ok;
    last_service_ = now;
    inputs_seen_ = true;
    if (dispatch_seen_ && uint32_t(now - dispatched_at_) >= REPEAT_GUARD_MS) dispatch_seen_ = false;
    if (cooldown_ && uint32_t(now - dispatched_at_) >= config_.lockout_ms) cooldown_ = false;
    if (stopped_) return;
    if (!config_.bench_mode && config_valid_ && !manually_disabled_ && !armed_ &&
        hardware_ok_) {
      armed_ = true;
      phase_ = idle_phase_();
      if (reason_ == ActionReason::DISARMED) reason_ = ActionReason::READY;
    }
    if (pending() && gap > MAX_SERVICE_GAP_MS) { disarm(ActionReason::LOOP_STALL); return; }
    if (config_.bench_mode && armed_ && uint32_t(now - armed_at_) >= ARM_SESSION_MS) { disarm(ActionReason::EXPIRED); return; }
    if (armed_ && !hardware_ok_) { disarm(ActionReason::HARDWARE_MISMATCH); return; }
    // MVP readiness includes offline local use. Only a network-originated
    // pending action (or the unchanged bench session) requires the Wi-Fi link.
    if (armed_ && !link_ok_ && (config_.bench_mode || (pending() && needs_link_(action_source_)))) {
      disarm(ActionReason::LINK_DOWN); return;
    }
    if (armed_ && !known_() && (config_.bench_mode || (pending() && action_command_ != DoorCommand::TOGGLE))) {
      disarm(ActionReason::UNKNOWN_STATE); return;
    }
    if (phase_ == ActionPhase::WARNING) {
      if (state_ != warning_state_ && (config_.bench_mode || action_command_ != DoorCommand::TOGGLE)) {
        cancel(now, ActionReason::STATE_CHANGED); return;
      }
      const auto elapsed = uint32_t(now - warning_at_);
      const bool warning_complete = outputs_.warning_tick(elapsed);
      if (elapsed < config_.warning_ms || !warning_complete) return;
      outputs_.warning_stop();
      dispatch_(now);
    } else if (phase_ == ActionPhase::PULSING) {
      if (!outputs_.pulse_active()) phase_ = idle_phase_();
      else if (uint32_t(now - dispatched_at_) > config_.pulse_ms + MAX_SERVICE_GAP_MS)
        disarm(ActionReason::OUTPUT_FAILURE);
    } else if (phase_ == ActionPhase::LOCKOUT && !cooldown_) {
      phase_ = ActionPhase::IDLE;
    }
  }
  bool arm_locally(uint32_t now) {
    if (stopped_) return reject_(ActionReason::SHUTDOWN);
    if (!config_valid_) return reject_(ActionReason::CONFIG_INVALID);
    if (!inputs_seen_ || uint32_t(now - last_service_) > MAX_SERVICE_GAP_MS)
      return reject_(ActionReason::LOOP_STALL);
    if (!hardware_ok_) return reject_(ActionReason::HARDWARE_MISMATCH);
    if (config_.bench_mode && !link_ok_) return reject_(ActionReason::LINK_DOWN);
    if (config_.bench_mode && !known_()) return reject_(ActionReason::UNKNOWN_STATE);
    if (config_.bench_mode && cooldown_) return reject_(ActionReason::LOCKOUT);
    if (pending()) return reject_(ActionReason::BUSY);
    armed_ = true;
    armed_at_ = now;
    manually_disabled_ = false;
    phase_ = ActionPhase::IDLE;
    reason_ = ActionReason::ARMED;
    return true;
  }
  bool request_toggle(uint32_t now) { return request(now, DoorCommand::TOGGLE); }
  bool request(uint32_t now, DoorCommand command) {
    return request_(now, command, CommandSource::NETWORK);
  }
  // Only the physical ControlButton uses this entrypoint. API/web/cover paths
  // keep the network default; callers cannot choose an origin through YAML.
  bool request_local_toggle(uint32_t now) { return request_(now, DoorCommand::TOGGLE, CommandSource::LOCAL_BUTTON); }
  void cancel(uint32_t, ActionReason reason = ActionReason::CANCELED) {
    outputs_.stop();  // Cut a contact pulse short if necessary; not a physical door Stop command.
    phase_ = !armed_ ? ActionPhase::DISARMED : idle_phase_();
    reason_ = reason;
  }
  void disarm(ActionReason reason = ActionReason::DISARMED) {
    if (!config_.bench_mode && reason == ActionReason::DISARMED) manually_disabled_ = true;
    if (reason == ActionReason::UNKNOWN_STATE || reason == ActionReason::HARDWARE_MISMATCH)
      outputs_.stop_for_readiness();
    else
      outputs_.stop();
    armed_ = false;
    phase_ = ActionPhase::DISARMED;
    reason_ = reason;  // Deliberately retain cooldown across disarm/rearm attempts.
  }
  void shutdown() { disarm(ActionReason::SHUTDOWN); stopped_ = true; }
  void reject_unsupported() { reason_ = ActionReason::UNSUPPORTED; }
  bool armed() const { return armed_; }
  bool bench_mode() const { return config_.bench_mode; }
  bool endpoint_ready() const { return endpoint_(); }
  bool pending() const { return phase_ == ActionPhase::WARNING || phase_ == ActionPhase::PULSING; }
  bool stopped() const { return stopped_; }
  ActionPhase phase() const { return phase_; }
  ActionReason reason() const { return reason_; }
  uint32_t dispatches() const { return dispatches_; }

 protected:
  bool needs_link_(CommandSource source) const { return config_.bench_mode || source != CommandSource::LOCAL_BUTTON; }
  bool request_(uint32_t now, DoorCommand command, CommandSource source) {
    if (!armed_ || stopped_) return reject_(ActionReason::DISARMED);
    if (pending()) return reject_(ActionReason::BUSY);
    const bool wall_toggle = !config_.bench_mode && command == DoorCommand::TOGGLE;
    const bool stopped_direction = !config_.bench_mode &&
        (command == DoorCommand::OPEN || command == DoorCommand::CLOSE) &&
        state_ == DoorState::STOPPED && outputs_.supports_stopped_direction();
    if (cooldown_ && !wall_toggle && !stopped_direction)
      return reject_(config_.bench_mode ? ActionReason::LOCKOUT : ActionReason::TARGET_LOCKOUT);
    if (!config_.bench_mode && dispatch_seen_ && uint32_t(now - dispatched_at_) < REPEAT_GUARD_MS)
      return reject_(ActionReason::REPEAT_GUARD);
    if (!inputs_seen_ || uint32_t(now - last_service_) > MAX_SERVICE_GAP_MS) {
      disarm(ActionReason::LOOP_STALL); return false;
    }
    if (config_.bench_mode && uint32_t(now - armed_at_) >= ARM_SESSION_MS) { disarm(ActionReason::EXPIRED); return false; }
    if (!hardware_ok_ || (config_.bench_mode && !known_())) {
      disarm(!hardware_ok_ ? ActionReason::HARDWARE_MISMATCH : ActionReason::UNKNOWN_STATE);
      return false;
    }
    // Sec+ 2.0 can explicitly resume either direction from confirmed Stopped.
    // Other non-endpoint cover refusals must not disable the wall-button action.
    if (!wall_toggle && !endpoint_() && !stopped_direction) return reject_(ActionReason::UNKNOWN_STATE);
    if (needs_link_(source) && !link_ok_) {
      if (config_.bench_mode) disarm(ActionReason::LINK_DOWN);
      return reject_(ActionReason::LINK_DOWN);  // A rejected remote request must not disable local MVP use.
    }
    if ((command == DoorCommand::OPEN && state_ == DoorState::OPEN) ||
        (command == DoorCommand::CLOSE && state_ == DoorState::CLOSED))
      return reject_(ActionReason::ALREADY_AT_TARGET);
    warning_state_ = state_;
    action_source_ = source;  // Rejected requests cannot reclassify an existing action.
    action_command_ = command;
    // Only actual protocol motion reports qualify. Distance/contact state and
    // time since a prior command are not evidence that the door is moving.
    if (wall_toggle && outputs_.reports_motion() &&
        (state_ == DoorState::OPENING || state_ == DoorState::CLOSING))
      return dispatch_(now);
    warning_at_ = now;
    phase_ = ActionPhase::WARNING;
    reason_ = ActionReason::WARNING;
    outputs_.warning_start(config_.warning_ms);
    return true;
  }
  bool endpoint_() const { return state_ == DoorState::CLOSED || state_ == DoorState::OPEN; }
  bool known_() const {
    return endpoint_() || (!config_.bench_mode && outputs_.reports_motion() &&
        (state_ == DoorState::OPENING || state_ == DoorState::CLOSING || state_ == DoorState::STOPPED));
  }
  ActionPhase idle_phase_() const {
    return config_.bench_mode && cooldown_ ? ActionPhase::LOCKOUT : ActionPhase::IDLE;
  }
  bool dispatch_(uint32_t now) {
    const bool wall_toggle = !config_.bench_mode && action_command_ == DoorCommand::TOGGLE;
    // UNKNOWN is the position-independent, fully warned Toggle contract.
    // Immediate moving Toggles retain their observed-motion binding.
    const auto expected = phase_ == ActionPhase::WARNING ? DoorState::UNKNOWN : warning_state_;
    const bool sent = wall_toggle ? outputs_.toggle(config_.pulse_ms, expected) :
        action_command_ == DoorCommand::TOGGLE ? outputs_.pulse(config_.pulse_ms) :
        outputs_.directed(config_.pulse_ms, action_command_ == DoorCommand::OPEN, warning_state_);
    if (!sent) { disarm(ActionReason::OUTPUT_FAILURE); return false; }
    ++dispatches_;
    dispatched_at_ = now;
    dispatch_seen_ = cooldown_ = true;
    phase_ = ActionPhase::PULSING;
    reason_ = ActionReason::DISPATCHED;
    return true;
  }
  bool reject_(ActionReason reason) { reason_ = reason; return false; }
  ControlOutputs &outputs_;
  ActionConfig config_;
  DoorState state_{DoorState::UNKNOWN}, warning_state_{DoorState::UNKNOWN};
  ActionPhase phase_{ActionPhase::DISARMED};
  CommandSource action_source_{CommandSource::NETWORK};
  DoorCommand action_command_{DoorCommand::TOGGLE};
  ActionReason reason_{ActionReason::DISARMED};
  uint32_t last_service_{0}, warning_at_{0}, dispatched_at_{0}, armed_at_{0}, dispatches_{0};
  bool armed_{false}, cooldown_{false}, inputs_seen_{false}, hardware_ok_{false}, link_ok_{false};
  bool config_valid_{true}, stopped_{false}, manually_disabled_{false};
  bool dispatch_seen_{false};
};

// Physical-only owner: no HA/YAML reset action.
class ButtonRecovery {
 public:
  virtual ~ButtonRecovery() = default;
  virtual void button_hold(bool factory) = 0;
  virtual void button_release(bool factory) = 0;
  virtual void button_report_ip() {}
};

// Cancellation consumes the door gesture, but a production recovery hold may
// continue. Only a debounced release commits a reset; never a threshold crossing.
class ControlButton {
 public:
  static constexpr uint32_t REPORT_IP_MS = 800;
  static constexpr uint32_t AP_RESET_MS = 4500;
  static constexpr uint32_t FACTORY_RESET_MS = 9500;
  void set_recovery(ButtonRecovery *recovery) { recovery_ = recovery; }
  void update(uint32_t now, bool pressed, ActionController &controller) {
    if (!initialized_) {
      initialized_ = true; raw_ = stable_ = pressed; changed_at_ = now;
      consumed_ = ignore_boot_hold_ = pressed;  // Held at boot must first be released.
      return;
    }
    if (pressed && controller.pending()) {
      controller.cancel(now);
      consumed_ = true;  // Immediate raw-level cancellation has precedence over a due dispatch.
    }
    if (pressed != raw_) { raw_ = pressed; changed_at_ = now; }
    if (raw_ != stable_ && uint32_t(now - changed_at_) >= 50) {
      stable_ = raw_;
      if (stable_) { pressed_at_ = changed_at_; hold_stage_ = 0; }
      else {
        const auto duration = uint32_t(changed_at_ - pressed_at_);
        const bool ignore = ignore_boot_hold_, canceled = consumed_;
        ignore_boot_hold_ = consumed_ = false;
        hold_stage_ = 0;
        if (ignore) return;
        if (!controller.bench_mode() && duration > AP_RESET_MS) {
          if (recovery_) recovery_->button_release(duration > FACTORY_RESET_MS);
          return;  // Even without a recovery owner, a long hold never toggles.
        }
        if (canceled) return;
        if (duration <= 50) return;
        if (!controller.bench_mode() && duration > REPORT_IP_MS) {
          if (recovery_) recovery_->button_report_ip();
          return; // Missing IP/report owner must never turn this into a door action.
        }
        if (controller.bench_mode() && duration >= 3000) {
          if (controller.armed()) controller.disarm();
          else controller.arm_locally(now);
        } else if (controller.armed()) controller.request_local_toggle(now);
        return;
      }
    }
    // Classify by raw release time, not the end of its debounce interval.
    if (stable_ && raw_ && !ignore_boot_hold_ && !controller.bench_mode()) {
      const auto duration = uint32_t(now - pressed_at_);
      const uint8_t stage = duration > FACTORY_RESET_MS ? 2 : duration > AP_RESET_MS ? 1 : 0;
      if (stage > hold_stage_) {
        hold_stage_ = stage;
        if (recovery_) recovery_->button_hold(stage == 2);
      }
    }
  }
 protected:
  uint32_t changed_at_{0}, pressed_at_{0};
  bool initialized_{false}, raw_{false}, stable_{false}, consumed_{false}, ignore_boot_hold_{false};
  uint8_t hold_stage_{0};
  ButtonRecovery *recovery_{nullptr};
};

}  // namespace esphome::opengarage
