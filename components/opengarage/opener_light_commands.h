// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace esphome::opengarage {
class OpenerLightCommands {
 public:
  virtual void request_light(bool target) = 0;
};
}  // namespace esphome::opengarage
