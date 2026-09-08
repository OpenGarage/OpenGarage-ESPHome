// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/defines.h"
#if defined(USE_OPENGARAGE_PULSE_MVP) || defined(USE_OPENGARAGE_SECPLUS1_CONTROL) || defined(USE_OPENGARAGE_SECPLUS2_CONTROL)
#include "action_controller.h"
#include "esphome/components/web_server_base/web_server_base.h"

namespace esphome::opengarage {
class UpdateGate {
 public:
  explicit UpdateGate(ActionController &controller) : controller_(controller) {}
  bool open() const { return open_; }
  void enter() {
    controller_.shutdown();  // Stop both outputs before allowing any upload request through.
    open_ = true;
  }
 protected:
  ActionController &controller_;
  bool open_{false};  // Once opened, latched until reboot.
};

// Registered before upstream OTA. A rejected request stays bound here, even if
// maintenance is entered later. WebServerBase applies its normal auth wrapper.
class UpdateGuard : public AsyncWebHandler {
 public:
  explicit UpdateGuard(const UpdateGate *gate) : gate_(gate) {}
  bool canHandle(AsyncWebServerRequest *request) const override {
    return request->method() == HTTP_POST && request->url() == "/update" && !gate_->open();
  }
  void handleRequest(AsyncWebServerRequest *request) override {
    request->send(409, "text/plain", "Enter Firmware Update Mode first; door controls stay disabled until reboot.");
  }
  void handleUpload(AsyncWebServerRequest *, const String &, size_t, uint8_t *, size_t, bool) override {}
  void handleBody(AsyncWebServerRequest *, uint8_t *, size_t, size_t, size_t) override {}
  bool isRequestHandlerTrivial() const override { return false; }
 protected:
  const UpdateGate *gate_;
};
}  // namespace esphome::opengarage
#endif
