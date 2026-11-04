/* VGMTrans (c) 2026. Licensed under the zlib license; see LICENSE. */
#include "CPSFurnaceSequence.h"
#include "CPSVolumeTable.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>
#include <tuple>

namespace conversion {
using Kind = SequenceEvent::Kind;
int cpsVolumeToFurnace(int raw) {
  if (raw < 0 || raw > 127) throw std::invalid_argument("CPS volume outside 0..127");
  // Preserve the parser's normalized amplitude, not its MIDI CC curve.
  return (255 * cpsVolumeTable[raw] + 4095) / 8191;
}
int cpsPanToFurnace(int raw) {
  if (raw < 0 || raw > 32) throw std::invalid_argument("CPS pan outside 0..32");
  // Choose the lowest 80xx value that reaches this QSound position in v228.
  return (255 * raw + 31) / 32;
}
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
  std::array<CPSSequencePlan::InitialState, 16> levels{};
  int globalTranspose = 0;
  std::map<std::pair<unsigned, unsigned>, unsigned> instruments;
  std::map<int, unsigned> ignored;
  for (const auto& action : actions) {
    const auto& e = action.event;
    auto& s = state[e.slot];
    plan.endTick = std::max(plan.endTick, e.tick);
    switch (e.kind) {
      case Kind::Volume:
      case Kind::Panning:
        if (e.domain != SequenceEvent::ValueDomain::SourceRaw)
          throw std::invalid_argument("CPS controls require raw source values, not native helper levels");
        if (e.kind == Kind::Volume) {
          cpsVolumeToFurnace(e.value);
          levels[e.slot].volume = e.value; levels[e.slot].volumeExplicit = true;
        } else {
          cpsPanToFurnace(e.value);
          levels[e.slot].pan = e.value; levels[e.slot].panExplicit = true;
        }
        plan.controls.push_back({e.tick, e.slot, e.kind, e.value, action.id - 1});
        break;
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
        plan.initialStates.try_emplace(e.slot, levels[e.slot]);
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
                              std::vector<std::string>& diagnostics, CPSControlReport* controlReport) {
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
    cell.instrument.reset();
    if (!note.off) {
      cell.instrument = resources.mappings.at(note.instrument).instrument;

    }
  }
  // Initialize only musical channels. These defaults are fallback assumptions,
  // not fabricated source events, and are reported separately from source writes.
  for (const auto& [slot, state] : plan.initialStates) {
    auto& cell = song.channels[slot].patterns[0].rows[0];
    cell.volume = cpsVolumeToFurnace(127);
    cell.effects.push_back({0x80, cpsPanToFurnace(16)});
    diagnostics.push_back("First Note On state: slot=" + std::to_string(slot) +
      " volumeCPS=" + std::to_string(state.volume) + " volumeFurnace=" + std::to_string(cpsVolumeToFurnace(state.volume)) +
      (state.volumeExplicit ? " explicit" : " assumed-full-volume") +
      " panCPS=" + std::to_string(state.pan) + " pan80xx=" + std::to_string(cpsPanToFurnace(state.pan)) +
      (state.panExplicit ? " explicit" : " assumed-center"));
  }
  CPSControlReport report;
  std::set<std::tuple<int, unsigned, Kind>> occupied;
  for (const auto& control : plan.controls) {
    const auto row = clock.row(control.tick);
    const bool volume = control.kind == Kind::Volume;
    const int value = volume ? cpsVolumeToFurnace(control.raw) : cpsPanToFurnace(control.raw);
    auto& stats = volume ? report.volume : report.panning;
    if (stats.executed++ == 0) {
      stats.sourceMin = stats.sourceMax = control.raw;
      stats.targetMin = stats.targetMax = value;
    }
    stats.sourceMin = std::min(stats.sourceMin, control.raw); stats.sourceMax = std::max(stats.sourceMax, control.raw);
    stats.targetMin = std::min(stats.targetMin, value); stats.targetMax = std::max(stats.targetMax, value);
    if (!occupied.emplace(control.slot, row, control.kind).second) {
      ++stats.merged;
      diagnostics.push_back(std::string(volume ? "Volume" : "Pan") + " merge: slot=" + std::to_string(control.slot) +
        " row=" + std::to_string(row) + " execution=" + std::to_string(control.order) + "; final value wins, intermediate state lost");
    } else ++stats.written;
    auto& channel = song.channels.at(control.slot);
    auto& cell = channel.patterns.at(row / 256).rows.at(row % 256);
    if (cell.note && cell.note->kind == module::Note::Kind::Pitch)
      diagnostics.push_back(std::string(volume ? "Volume" : "Pan") + " shares Note On row: slot=" +
        std::to_string(control.slot) + " row=" + std::to_string(row) + " execution=" + std::to_string(control.order) +
        "; row uses final control state (sub-row ordering unavailable)");
    if (volume) cell.volume = value;
    else {
      auto it = std::find_if(cell.effects.begin(), cell.effects.end(), [](const auto& fx) { return fx.command == 0x80; });
      if (it != cell.effects.end()) it->value = value;
      else cell.effects.push_back({0x80, value});
      channel.effectColumns = std::max<unsigned>(channel.effectColumns, cell.effects.size());
    }
  }
  if (controlReport) *controlReport = report;
  auto& stopCell = song.channels[0].patterns[stopRow / 256].rows[stopRow % 256];
  stopCell.effects.push_back({0xff, 0});
  song.channels[0].effectColumns = std::max<unsigned>(song.channels[0].effectColumns, stopCell.effects.size());
  return song;
}
}
