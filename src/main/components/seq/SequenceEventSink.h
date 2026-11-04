/* VGMTrans (c) 2026. Licensed under the zlib license; see LICENSE. */
#pragma once
#include <cstdint>
#include <vector>

// Values from a particular parser execution, never references to UI or MIDI events.
struct SequenceEvent {
  enum class Kind { NoteOn, NoteDuration, NoteOff, Bank, Program, QSoundTempo,
                    GlobalTranspose, End, Loop, Ignored, Boundary, Volume, Panning };
  enum class ValueDomain { NativeHelper, SourceRaw };
  Kind kind;
  std::uint32_t tick;
  int slot; // Source hardware slot; -1 means the parser has not supplied one.
  std::uint32_t offset;
  // Note keys use VGMTrans chromatic key convention (60 = middle C),
  // including local transpose. Bank is the logical 128-program bank, not CC.
  // QSoundTempo retains the original 16-bit driver operand.
  int value = 0;
  std::uint32_t duration = 0;
  ValueDomain domain = ValueDomain::NativeHelper;
  std::uint64_t order = 0; // Capture-wide execution ordinal; independent of ROM offset.
};
class SequenceEventSink {
 public:
  virtual ~SequenceEventSink() = default;
  virtual void onSequenceEvent(const SequenceEvent& event) = 0;
};
class SequenceTimeline final : public SequenceEventSink {
 public:
  std::vector<SequenceEvent> events;
  void onSequenceEvent(const SequenceEvent& event) override { events.push_back(event); }
};
