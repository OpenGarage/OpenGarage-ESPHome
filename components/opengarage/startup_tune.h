// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Arduino.h>
#include <cstdint>
namespace esphome::opengarage {
enum class StartupTune : uint8_t { AP, STATION, SETUP_SUCCESS };
class StartupMelody {
 public:
  void start(uint32_t now, StartupTune tune) {
    cancel(); tune_=tune; index_=0; gap_=false; active_=true;
    at_=now; wait_=0; tick(now);
  }
  void cancel() { if (active_) noTone(13); active_=false; }
  bool active() const { return active_; }
  void tick(uint32_t now) {
    const auto elapsed=uint32_t(now-at_);
    if (!active_ || elapsed>=0x80000000U || elapsed<wait_) return;
    at_=now;
    const unsigned count=tune_==StartupTune::SETUP_SUCCESS?6:3;
    if (gap_) {
      noTone(13);
      if (++index_==count) { active_=false; return; }
      gap_=false; wait_=40; return;
    }
    static constexpr unsigned ap[]={262,330,392}; // C4 E4 G4
    static constexpr unsigned station[]={330,392,523}; // E4 G4 C5
    static constexpr unsigned success[]={196,262,330,392,330,392}; // G3 C4 E4 G4 E4 G4
    const auto *notes=tune_==StartupTune::AP?ap:tune_==StartupTune::STATION?station:success;
    // Quarter = 120 ms, half = 240 ms, whole = 480 ms; 40 ms articulation gaps.
    static constexpr unsigned success_beats[]={1,1,1,2,1,4};
    const unsigned beats=tune_==StartupTune::SETUP_SUCCESS?success_beats[index_]:
        tune_==StartupTune::STATION && index_==2?2:1;
    wait_=120*beats;
    tone(13,notes[index_],wait_); gap_=true;
  }
 protected:
  uint32_t at_{0},wait_{0};
  unsigned index_{0};
  StartupTune tune_{StartupTune::AP};
  bool active_{false},gap_{false};
};
} // namespace esphome::opengarage
