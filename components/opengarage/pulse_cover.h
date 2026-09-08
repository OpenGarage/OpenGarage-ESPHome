// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/defines.h"
#if defined(USE_OPENGARAGE_PULSE_MVP) || defined(USE_OPENGARAGE_SECPLUS1_CONTROL)
#include "action_controller.h"
#include "esphome/components/cover/cover.h"

namespace esphome::opengarage {
class CoverCommands {
 public:
  virtual void request_door(DoorCommand command) = 0;
  virtual void reject_cover_command() = 0;
};

// Native covers have no missing-state field. Retain the last binary indication
// as assumed; companion Door State / State Valid entities are authoritative on faults.
class PulseCover : public cover::Cover {
 public:
  explicit PulseCover(CoverCommands *parent) : parent_(parent) {}
  cover::CoverTraits get_traits() override {
    cover::CoverTraits traits;
    traits.set_is_assumed_state(true);
    traits.set_supports_position(false);
    traits.set_supports_tilt(false);
    traits.set_supports_stop(false);
    traits.set_supports_toggle(false);
    return traits;
  }
  void observe(DoorState state) {
    if (state != DoorState::OPEN && state != DoorState::CLOSED) return;
    const float next = state == DoorState::OPEN ? cover::COVER_OPEN : cover::COVER_CLOSED;
    if (!published_ || position != next) {
      published_ = true;
      position = next;
      current_operation = cover::COVER_OPERATION_IDLE;
      publish_state(false);  // No restored target or cover-state preference writes.
    }
  }
 protected:
  void control(const cover::CoverCall &call) override {
    if (call.get_stop() || call.get_toggle().has_value() || call.get_tilt().has_value()) {
      parent_->reject_cover_command();
      return;
    }
    if (!call.get_position().has_value()) return;
    if (*call.get_position() == cover::COVER_OPEN) parent_->request_door(DoorCommand::OPEN);
    else if (*call.get_position() == cover::COVER_CLOSED) parent_->request_door(DoorCommand::CLOSE);
    else parent_->reject_cover_command();
  }
  CoverCommands *parent_;
  bool published_{false};
};
}  // namespace esphome::opengarage
#endif
