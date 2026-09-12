// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifdef OG_SEC1_TRACE
#include <array>
#include <cstdio>
#include "esphome/core/log.h"

namespace esphome::opengarage {
// Opt-in development diagnostics. Main-loop only; no allocation or logging in RX
// decode/transmit paths. RX timestamps are dequeue times, NOT wire-edge times.
struct Sec1TraceEvent { uint32_t us; uint16_t detail; uint8_t byte; char kind; };
inline std::array<Sec1TraceEvent, 64> sec1_trace_buffer{};
inline size_t sec1_trace_head = 0, sec1_trace_size = 0;
inline uint32_t sec1_trace_dropped = 0, sec1_trace_flush_ms = 0;
inline void sec1_trace(char kind, uint32_t us, uint8_t byte, uint16_t detail) {
  if (sec1_trace_size == sec1_trace_buffer.size()) { ++sec1_trace_dropped; return; }
  sec1_trace_buffer[(sec1_trace_head + sec1_trace_size++) % sec1_trace_buffer.size()] = {us, detail, byte, kind};
}
inline void sec1_trace_flush(uint32_t now) {
  if (uint32_t(now - sec1_trace_flush_ms) < 250) return;
  sec1_trace_flush_ms = now;
  if (!sec1_trace_size && !sec1_trace_dropped) return;
  char line[320]{};
  size_t used = 0;
  for (unsigned n = 0; n < 8 && sec1_trace_size; ++n) {
    const auto &e = sec1_trace_buffer[sec1_trace_head];
    const int len = std::snprintf(line + used, sizeof(line) - used, "%c@%lu:%02X/%u ",
        e.kind, static_cast<unsigned long>(e.us), unsigned(e.byte), unsigned(e.detail));
    if (len < 0 || size_t(len) >= sizeof(line) - used) break;
    used += size_t(len);
    sec1_trace_head = (sec1_trace_head + 1) % sec1_trace_buffer.size();
    --sec1_trace_size;
  }
  // WARN allows the existing read-only collector to retain this diagnostic build.
  ESP_LOGW("og.sec1trace", "drop=%lu %s", static_cast<unsigned long>(sec1_trace_dropped), line);
  sec1_trace_dropped = 0;
}
}
#else
// Discard arguments too: an inline no-op would still evaluate micros() calls.
#define sec1_trace(...) ((void) 0)
#define sec1_trace_flush(...) ((void) 0)
#endif
