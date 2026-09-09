// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "action_controller.h"

namespace esphome::opengarage {

// Presentation only. Does not open the upload gate, resume controls, or send work.
// Keep the indication independent of Last Action Reason: a rejected command or
// Cancel must not hide the latched update mode. A new instance starts inactive.
class UpdateStatus {
 public:
  void prepare() { if (state_ == State::INACTIVE) state_ = State::PREPARED; }
  void started() { state_ = State::UPLOADING; }
  void failed() { state_ = State::FAILED; }
  void completed() { state_ = State::COMPLETED; }
  const char *phase(ActionPhase normal, bool protocol) const {
    switch (state_) {
      case State::PREPARED: return "Firmware update mode";
      case State::UPLOADING: return "Updating firmware";
      case State::FAILED: return "Firmware update failed";
      case State::COMPLETED: return "Firmware update complete";
      default: return protocol && normal == ActionPhase::PULSING ?
          "Releasing protocol button" : action_phase_name(normal);
    }
  }
  const char *reason(ActionReason normal, bool protocol) const {
    switch (state_) {
      case State::PREPARED: return "Upload firmware, or press Restart Device to exit";
      case State::UPLOADING: return "Upload in progress; do not restart or remove power";
      case State::FAILED: return "Upload failed/aborted; restart only when no upload is active";
      case State::COMPLETED: return "Upload complete; device restarting";
      default: return protocol && normal == ActionReason::DISPATCHED ?
          "Door command sent" : action_reason_name(normal);
    }
  }
 protected:
  enum class State : uint8_t { INACTIVE, PREPARED, UPLOADING, FAILED, COMPLETED };
  State state_{State::INACTIVE};
};

}  // namespace esphome::opengarage
