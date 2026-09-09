// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace esphome::opengarage {
enum class OpenerProtocol : uint8_t { UNCONFIGURED, PULSE, SECPLUS1, SECPLUS2 };
enum class PanelEmulation : uint8_t { AUTOMATIC, DISABLED };
struct ProtocolSettings {
  OpenerProtocol protocol{OpenerProtocol::UNCONFIGURED};
  PanelEmulation panel{PanelEmulation::AUTOMATIC};
  uint32_t encode() const { return 0x4F470000U | (uint32_t(panel) << 8) | uint32_t(protocol); }
  bool decode(uint32_t value) {
    if ((value & 0xFFFFFEFCU) != 0x4F470000U) return false;
    protocol = OpenerProtocol(value & 3U); panel = PanelEmulation((value >> 8) & 1U); return true;
  }
  bool operator==(const ProtocolSettings &other) const { return encode() == other.encode(); }
  bool operator!=(const ProtocolSettings &other) const { return !(*this == other); }
};
inline bool security_protocol(OpenerProtocol value) {
  return value == OpenerProtocol::SECPLUS1 || value == OpenerProtocol::SECPLUS2;
}
inline const char *protocol_name(OpenerProtocol value) {
  switch (value) {
    case OpenerProtocol::PULSE: return "None (dry contact)";
    case OpenerProtocol::SECPLUS1: return "Security+ 1.0";
    case OpenerProtocol::SECPLUS2: return "Security+ 2.0";
    default: return "Not configured";
  }
}
// Entity exposure is fixed for this boot. Mask bits correspond to OpenerProtocol.
inline bool protocol_exposes(OpenerProtocol value, uint8_t mask) { return (mask & (1U << uint8_t(value))) != 0; }
}  // namespace esphome::opengarage
