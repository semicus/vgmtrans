/* VGMTrans (c) 2026. Licensed under the zlib license; see LICENSE. */
#pragma once
#include "CPSFurnaceAdapter.h"
#include "SequenceEventSink.h"
#include <set>

namespace conversion {
// Integrates absolute sequence ticks, retaining unrounded segment boundaries.
class QSoundTimelineClock {
 public:
  explicit QSoundTimelineClock(const std::vector<SequenceEvent>& events);
  long double seconds(std::uint32_t tick) const;
  std::uint32_t row(std::uint32_t tick) const;
 private:
  struct Segment { std::uint32_t tick; int tempo; long double seconds; };
  std::vector<Segment> segments;
};
struct CPSSequencePlan {
  struct Note { std::uint32_t tick; int slot, key; bool off; unsigned instrument; };
  std::vector<Note> notes;
  std::vector<CPSInstrumentSelection> selection;
  std::set<int> activeSlots;
  std::vector<std::string> diagnostics;
  unsigned noteOns = 0, noteOffs = 0, programs = 0, tempos = 0;
  std::uint32_t endTick = 0;
};
CPSSequencePlan planCPSSequence(const SequenceTimeline& timeline);
module::Song renderCPSSequence(const CPSSequencePlan& plan, const QSoundTimelineClock& clock,
                              const CPSFurnaceResources& resources, unsigned& collisions,
                              std::vector<std::string>& diagnostics);
}
