// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/defines.h"
#ifdef USE_OPENGARAGE_UNIFIED
#include "protocol_mode.h"
#include "esphome/components/select/select.h"
namespace esphome::opengarage {
class ProtocolSettingCommands {
 public:
  virtual void request_setting(bool panel, size_t index) = 0;
};
class ProtocolSelect : public select::Select {
 public:
  ProtocolSelect(ProtocolSettingCommands *parent, bool panel) : parent_(parent), panel_(panel) {}
 protected:
  void control(const std::string &value) override {
    const auto index = index_of(value);
    if (index) parent_->request_setting(panel_, *index);
  }
  ProtocolSettingCommands *parent_;
  bool panel_;
};
}  // namespace esphome::opengarage
#endif
