// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

namespace esphome::opengarage {

enum class DoorState : uint8_t { UNKNOWN, CLOSED, OPEN, OPENING, CLOSING, STOPPED };
enum class VehicleState : uint8_t { UNAVAILABLE, UNKNOWN, ABSENT, PRESENT };
enum class StateSource : uint8_t { DISTANCE, CONTACT, BOTH_AND, EITHER_OR, PROTOCOL };
enum class Mounting : uint8_t { CEILING, SIDE };
enum class ContactType : uint8_t { NONE, NORMALLY_CLOSED, NORMALLY_OPEN };

struct ResolverConfig {
  StateSource source{StateSource::DISTANCE};
  Mounting mounting{Mounting::CEILING};
  ContactType contact_type{ContactType::NONE};
  bool distance_enabled{true};
  uint16_t door_threshold_cm{50};
  uint16_t vehicle_threshold_cm{150};  // Zero disables vehicle inference.
};

struct ResolvedState {
  DoorState door{DoorState::UNKNOWN};
  VehicleState vehicle{VehicleState::UNAVAILABLE};
};

inline const char *door_state_name(DoorState state) {
  switch (state) {
    case DoorState::CLOSED: return "Closed";
    case DoorState::OPEN: return "Open";
    case DoorState::OPENING: return "Opening";
    case DoorState::CLOSING: return "Closing";
    case DoorState::STOPPED: return "Stopped";
    default: return "Unknown";
  }
}

inline const char *vehicle_state_name(VehicleState state) {
  switch (state) {
    case VehicleState::ABSENT: return "Absent";
    case VehicleState::PRESENT: return "Present";
    case VehicleState::UNKNOWN: return "Unknown";
    default: return "Unavailable";
  }
}

inline std::optional<bool> contact_open(ContactType type, std::optional<bool> pin_high) {
  if (!pin_high || type == ContactType::NONE) return {};
  return type == ContactType::NORMALLY_CLOSED ? *pin_high : !*pin_high;
}

// Mirrors the stock valid-input truth tables; invalid required inputs yield UNKNOWN.
inline ResolvedState resolve_state(const ResolverConfig &config, std::optional<uint16_t> distance_cm,
                                   std::optional<bool> contact_pin_high,
                                   DoorState protocol_state = DoorState::UNKNOWN) {
  if (!config.distance_enabled || (distance_cm && (*distance_cm == 0 || *distance_cm > 500)))
    distance_cm.reset();
  std::optional<bool> range_open;
  if (distance_cm) {
    range_open = *distance_cm <= config.door_threshold_cm;
    if (config.mounting == Mounting::SIDE) range_open = !*range_open;
  }
  const auto switch_open = contact_open(config.contact_type, contact_pin_high);
  std::optional<bool> open;
  ResolvedState result;
  switch (config.source) {
    case StateSource::DISTANCE: open = range_open; break;
    case StateSource::CONTACT: open = switch_open; break;
    case StateSource::BOTH_AND:
      if (range_open && switch_open) open = *range_open && *switch_open;
      break;
    case StateSource::EITHER_OR:
      if (range_open && switch_open) open = *range_open || *switch_open;
      break;
    case StateSource::PROTOCOL: result.door = protocol_state; break;
  }
  if (open) result.door = *open ? DoorState::OPEN : DoorState::CLOSED;

  if (!config.distance_enabled || config.vehicle_threshold_cm == 0 || config.mounting == Mounting::SIDE)
    return result;
  result.vehicle = VehicleState::UNKNOWN;
  if (!distance_cm) return result;
  const bool independent_door = config.source == StateSource::PROTOCOL || config.source == StateSource::CONTACT;
  if (!independent_door && range_open && *range_open) return result;  // Door obscures the sensor.
  result.vehicle = *distance_cm <= config.vehicle_threshold_cm ? VehicleState::PRESENT : VehicleState::ABSENT;
  return result;
}

}  // namespace esphome::opengarage
