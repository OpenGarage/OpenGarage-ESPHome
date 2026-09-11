// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace esphome::opengarage {
// Allocation-ordered ESP8266 preference record. Do not resize/reorder it in an
// OTA update. API, web/OTA and opener identity commit together, with the upstream
// preference checksum covering the entire record. Flash is NOT encrypted.
struct GenericCredentials {
  static constexpr uint32_t MAGIC = 0x4F474701U;
  uint32_t magic{MAGIC};
  uint32_t client_id{0};
  std::array<uint8_t, 32> api_key{};
  char admin[64]{};

  static bool password_valid(const std::string &password) {
    if (password.size() < 12 || password.size() > 63) return false;
    for (unsigned char c : password) if (c < 32 || c > 126) return false;
    return true;
  }
  bool valid() const {
    if (magic != MAGIC || client_id == 0 || std::memchr(admin, 0, sizeof(admin)) == nullptr) return false;
    uint8_t any = 0;
    for (auto b : api_key) any |= b;
    return any != 0 && password_valid(admin);
  }
};
static_assert(sizeof(GenericCredentials) == 104, "Generic OTA preference layout must remain stable");

}  // namespace esphome::opengarage
