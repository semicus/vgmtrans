/* VGMTrans (c) 2026. Licensed under the zlib license; see LICENSE. */
#include "CPSFurnaceSequence.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>
#include <tuple>

namespace conversion {
using Kind = SequenceEvent::Kind;
QSoundTimelineClock::QSoundTimelineClock(const std::vector<SequenceEvent>& events) {
  std::vector<SequenceEvent> tempos;
  for (const auto& e : events) if (e.kind == Kind::QSoundTempo) tempos.push_back(e);
  std::stable_sort(tempos.begin(), tempos.end(), [](auto& a, auto& b) { return a.tick < b.tick; });
  if (tempos.empty() || tempos.front().tick != 0)
    throw std::invalid_argument("An explicit QSound tempo at tick zero is required");
  for (const auto& e : tempos) {
    if (e.value <= 0 || e.value > 65535) throw std::invalid_argument("Invalid QSound tempo");
    if (!segments.empty() && e.tick == segments.back().tick) {
      if (e.value != segments.back().tempo) throw std::invalid_argument("Conflicting simultaneous tempos");
      continue;
    }
    const auto time = segments.empty() ? 0 : seconds(e.tick);
    segments.push_back({e.tick, e.value, time});
  }
}
long double QSoundTimelineClock::seconds(std::uint32_t tick) const {
  auto it = std::upper_bound(segments.begin(), segments.end(), tick,
                            [](auto t, const auto& s) { return t < s.tick; });
  const auto& s = *std::prev(it);
  return s.seconds + static_cast<long double>(tick - s.tick) * 256 / (s.tempo * 62.5L);
}
std::uint32_t QSoundTimelineClock::row(std::uint32_t tick) const {
  return static_cast<std::uint32_t>(std::llround(seconds(tick) * 62.5L));
}

CPSSequencePlan planCPSSequence(const SequenceTimeline& timeline) {
  CPSSequencePlan plan;
  struct Action { SequenceEvent event; size_t id; int priority; bool scheduled; };
  std::vector<Action> actions;
  for (size_t i = 0; i < timeline.events.size(); ++i) {
    const auto& e = timeline.events[i];
    if (e.slot < 0 || e.slot > 15) throw std::invalid_argument("Missing/invalid original QSound slot");
    plan.activeSlots.insert(e.slot);
    actions.push_back({e, i + 1, 1, false});
    if (e.kind == Kind::NoteDuration) {
      auto off = e; off.kind = Kind::NoteOff; off.tick += e.duration;
      actions.push_back({off, i + 1, e.duration ? 0 : 2, true});
    }
  }
  std::stable_sort(actions.begin(), actions.end(), [](const auto& a, const auto& b) {
    return std::tie(a.event.tick, a.priority, a.event.slot) < std::tie(b.event.tick, b.priority, b.event.slot);
  });
  struct State { int bank = 0, program = -1, key = -1; size_t voice = 0; };
  std::array<State, 16> state{};
  int globalTranspose = 0;
  std::map<std::pair<unsigned, unsigned>, unsigned> instruments;
  std::map<int, unsigned> ignored;
  for (const auto& action : actions) {
    const auto& e = action.event;
    auto& s = state[e.slot];
    plan.endTick = std::max(plan.endTick, e.tick);
    switch (e.kind) {
      case Kind::Bank: s.bank = e.value; break;
      case Kind::Program: s.program = e.value; ++plan.programs; break;
      case Kind::GlobalTranspose: globalTranspose = e.value; break;
      case Kind::QSoundTempo: ++plan.tempos; break;
      case Kind::Ignored: ++ignored[e.value]; break;
      case Kind::Loop:
        plan.diagnostics.push_back("Loop boundary: slot=" + std::to_string(e.slot) + " tick=" + std::to_string(e.tick)); break;
      case Kind::Boundary:
        if (e.value) plan.diagnostics.push_back("Capture truncated: slot=" + std::to_string(e.slot) + " reason=" + std::to_string(e.value));
        break;
      case Kind::NoteOn:
      case Kind::NoteDuration: {
        if (s.program < 0) throw std::invalid_argument("Note before explicit program selection");
        if (s.bank < 0 || s.bank > 3) throw std::invalid_argument("Unsupported V1.01 bank");
        const auto source = std::pair<unsigned,unsigned>{s.bank / 2, (s.bank % 2) * 128 + s.program};
        auto [it, added] = instruments.emplace(source, static_cast<unsigned>(plan.selection.size()));
        if (added) plan.selection.push_back({source.first, source.second});
        if (s.voice) plan.diagnostics.push_back("Monophonic retrigger replaces active note: slot=" + std::to_string(e.slot) + " tick=" + std::to_string(e.tick));
        s.voice = action.id; s.key = e.value;
        plan.notes.push_back({e.tick, e.slot, e.value + globalTranspose - 12, false, it->second});
        ++plan.noteOns;
        break;
      }
      case Kind::NoteOff:
        // Old keyed/timed offs must not silence a newer monophonic voice.
        if (s.voice && (action.scheduled ? s.voice == action.id : s.key == e.value)) {
          plan.notes.push_back({e.tick, e.slot, 0, true, 0});
          s.voice = 0; ++plan.noteOffs;
        } else {
          plan.diagnostics.push_back("Obsolete Note Off suppressed: slot=" + std::to_string(e.slot) + " tick=" + std::to_string(e.tick));
        }
        break;
      case Kind::End: break;
    }
  }
  for (int slot = 0; slot < 16; ++slot) if (state[slot].voice) {
    plan.notes.push_back({plan.endTick, slot, 0, true, 0});
    plan.diagnostics.push_back("Synthetic final Note Off: slot=" + std::to_string(slot));
  }
  for (auto [opcode, count] : ignored)
    plan.diagnostics.push_back("Ignored opcode (decimal)=" + std::to_string(opcode) + " count=" + std::to_string(count));
  return plan;
}

module::Song renderCPSSequence(const CPSSequencePlan& plan, const QSoundTimelineClock& clock,
                              const CPSFurnaceResources& resources, unsigned& collisions,
                              std::vector<std::string>& diagnostics) {
  module::Song song;
  song.systems.push_back({}); song.channels.resize(19);
  song.timing.rowsPerPattern = 256;
  song.samples = resources.samples; song.instruments = resources.instruments;
  const auto stopRow = clock.row(plan.endTick) + 1;
  const auto patterns = stopRow / 256 + 1;
  if (patterns > 256) throw std::invalid_argument("Sequence exceeds v228 order capacity");
  for (auto& channel : song.channels) for (unsigned p = 0; p < patterns; ++p) {
    channel.orders.push_back(static_cast<std::uint16_t>(p));
    channel.patterns.push_back({static_cast<std::uint16_t>(p), "", std::vector<module::Cell>(256)});
  }
  collisions = 0;
  for (const auto& note : plan.notes) {
    const auto row = clock.row(note.tick);
    auto& cell = song.channels.at(note.slot).patterns.at(row / 256).rows.at(row % 256);
    if (cell.note) {
      ++collisions;
      diagnostics.push_back("Quantization collision: slot=" + std::to_string(note.slot) + " row=" + std::to_string(row) + " tick=" + std::to_string(note.tick) + "; last event wins");
    }
    cell.note = module::Note{note.off ? module::Note::Kind::Off : module::Note::Kind::Pitch, note.key};
    cell.instrument.reset(); cell.volume.reset();
    if (!note.off) {
      cell.instrument = resources.mappings.at(note.instrument).instrument;
      cell.volume = 128;
    }
  }
  song.channels[0].patterns[stopRow / 256].rows[stopRow % 256].effects = {{0xff, 0}};
  return song;
}
}
