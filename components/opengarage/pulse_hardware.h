// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace esphome::opengarage {
// The GPIO10 strap identifies a hardware family, not opener protocol or
// dry-contact compatibility. A pulse build must match its selected family.
constexpr bool pulse_hardware_matches(bool selected_v23, bool detected_v23,
                                      bool bench_mode, bool bench_mac_matches) {
  return selected_v23 == detected_v23 &&
         (!bench_mode || (!selected_v23 && bench_mac_matches));
}
}  // namespace esphome::opengarage
