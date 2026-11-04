/* VGMTrans (c) 2026. Licensed under the zlib license; see LICENSE. */
#include "ResourceTestRoot.h"
#include "CPS/CPSFurnaceSequence.h"
#include "VGMSeq.h"
#include "CPS/CPS2Seq.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace conversion;
using K = SequenceEvent::Kind;
void check(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
class TestTrack : public SeqTrack {
 public:
  using SeqTrack::SeqTrack;
  int step = 0;
  void resetVars() override { SeqTrack::resetVars(); step = 0; }
  bool readEvent() override {
    switch (step++) {
      case 0:
        emitSequenceEvent(K::QSoundTempo, 256);
        addVolNoItem(100, 110); addPanNoItem(63, 16);
        addBankSelectNoItem(1); addProgramChangeNoItem(35, false);
        addNoteByDurNoItem(60, 100, 260); addTime(300); return true;
      case 1:
        // Same ROM address executed again, different state and absolute time.
        addBankSelectNoItem(2); addProgramChangeNoItem(1, false);
        addNoteOnNoItem(72, 100); addTime(20); return true;
      default:
        addNoteOffNoItem(72); addEndOfTrack(curOffset, 1); return false;
    }
  }
};
class TestSeq : public VGMSeq {
 public:
  explicit TestSeq(RawFile* raw) : VGMSeq("CPS2", raw, 0, 64) {}
  bool supportsEventCapture() const override { return true; }
  bool parseHeader() override { setPPQN(48); return true; }
  bool parseTrackPointers() override {
    addTrack<TestTrack>(this, 1, 16)->sourceSlot = 2;
    addTrack<TestTrack>(this, 32, 16)->sourceSlot = 9;
    return true;
  }
};
class LoopTrack : public SeqTrack {
 public:
  using SeqTrack::SeqTrack;
  bool spin = false;
  bool readEvent() override {
    if (spin && readMode == READMODE_CAPTURE_EVENTS) return true;
    addNoteByDurNoItem(60, 100, 1);
    addTime(2);
    return addLoopForever(curOffset, 1);
  }
};
class LoopSeq : public TestSeq {
 public:
  using TestSeq::TestSeq;
  LoopTrack* loopTrack = nullptr;
  bool parseTrackPointers() override {
    loopTrack = addTrack<LoopTrack>(this, 1, 16); loopTrack->sourceSlot = 0;
    return true;
  }
};
int main() {
  try {
    ResourceTestRoot root; root.init();
    std::array<u8, 64> bytes{}; VirtFile raw(bytes.data(), bytes.size(), "test.bin");
    TestSeq seq(&raw); check(seq.load(), "load");
    std::vector<u8> before, after;
    seq.convertToMidi()->writeMidiToBuffer(before);
    SequenceTimeline timeline; check(seq.captureEvents(timeline), "capture");
    check(seq.eventSink() == nullptr, "sink detached");
    for (size_t i = 0; i < timeline.events.size(); ++i) check(timeline.events[i].order == i, "capture-wide execution ordinal");
    unsigned rawControls = 0;
    for (const auto& e : timeline.events) if (e.kind == K::Volume || e.kind == K::Panning) {
      check(e.domain == SequenceEvent::ValueDomain::SourceRaw && e.value == (e.kind == K::Volume ? 110 : 16), "NoItem retains raw source value");
      ++rawControls;
    }
    check(rawControls == 4, "NoItem emits exactly one control per execution");
    auto plan = planCPSSequence(timeline);
    check(plan.activeSlots == std::set<int>({2, 9}), "sparse hardware slots");
    check(plan.noteOns == 4 && plan.noteOffs == 4, "note counts including NoItem");
    check(plan.selection.size() == 2 && plan.selection[0].bank == 0 && plan.selection[0].program == 163 && plan.selection[1].bank == 1 && plan.selection[1].program == 1, "bank folding");
    std::vector<SequenceEvent> notes;
    for (auto& e : timeline.events) if ((e.kind == K::NoteOn || e.kind == K::NoteDuration) && e.slot == 2) notes.push_back(e);
    check(notes.size() == 2 && notes[0].offset == notes[1].offset && notes[0].tick == 0 && notes[1].tick == 300, "execution identity");
    QSoundTimelineClock clock(timeline.events);
    CPSFurnaceResources resources;
    resources.mappings = {{0,163,1,35,0,60,0,0}, {1,1,2,1,1,60,1,1}};
    unsigned collisions; std::vector<std::string> diagnostics;
    auto song = renderCPSSequence(plan, clock, resources, collisions, diagnostics);
    check(collisions == 0 && song.channels.size() == 19, "render");
    check(song.channels[2].patterns[0].rows[0].note->semitone == 48, "C4 convention");
    check(song.channels[2].patterns[1].rows[4].note->kind == module::Note::Kind::Off, "off crosses pattern boundary");
    check(!song.channels[0].patterns[0].rows[0].note, "empty slot preserved");
    seq.convertToMidi()->writeMidiToBuffer(after); check(before == after, "MIDI unchanged after capture");
    SequenceTimeline repeated; seq.captureEvents(repeated); check(repeated.events.size() == timeline.events.size(), "repeat capture reset");
    struct ThrowSink : SequenceEventSink { void onSequenceEvent(const SequenceEvent&) override { throw std::runtime_error("test"); } } throwing;
    try { seq.captureEvents(throwing); } catch (const std::runtime_error&) {}
    check(seq.eventSink() == nullptr, "exception detaches sink");
    after.clear(); seq.convertToMidi()->writeMidiToBuffer(after); check(before == after, "MIDI after exception");
    std::vector<SequenceEvent> tempos{{K::QSoundTempo,0,0,0,512}, {K::QSoundTempo,3,0,0,1024}};
    QSoundTimelineClock changing(tempos);
    check(std::abs(changing.seconds(7) - 0.04L) < 1e-12L && changing.row(7) == 3, "tempo segment integration");
    check(changing.row(2) == 1, "absolute rounding, not per delta");
    bool rejected = false;
    try { QSoundTimelineClock missing({}); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "missing initial tempo rejected");
    tempos.push_back({K::QSoundTempo,3,9,0,512}); rejected = false;
    try { QSoundTimelineClock conflicting(tempos); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "tempo conflict rejected");
    plan.notes.push_back(plan.notes[0]);
    renderCPSSequence(plan, clock, resources, collisions, diagnostics);
    check(collisions == 1 && !diagnostics.empty(), "collision diagnostics");
    LoopSeq looping(&raw); check(looping.load(), "loop fixture load");
    for (int repeats : {0,2}) {
      SequenceTimeline loopTimeline; looping.captureEvents(loopTimeline, 100, repeats);
      unsigned count = 0;
      for (auto& e : loopTimeline.events) if (e.kind == K::NoteDuration) ++count;
      check(count == static_cast<unsigned>(repeats + 1), "bounded infinite loop expansion");
    }
    SequenceTimeline limited; looping.captureEvents(limited, 2, 10);
    check(limited.events.back().kind == K::Boundary && limited.events.back().value == 1, "tick limit diagnostic");
    looping.loopTrack->spin = true;
    rejected = false;
    try { SequenceTimeline spin; looping.captureEvents(spin); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected && looping.eventSink() == nullptr, "zero-time loop operation guard");
    // Real CPS parser over synthetic bytes: sparse pointer table and a finite
    // loop execute the same ROM note address three times. No ROM in CI.
    std::array<u8, 96> cpsBytes{};
    cpsBytes[2] = 33; cpsBytes[20] = 64; // slots 0 and 9
    const std::array<u8, 10> first{0x05,1,0,0x08,163,0xc1,0x0e,2,4,0x17};
    const std::array<u8, 11> second{0x05,1,0,0x1f,1,0x07,77,0x18,17,0xc1,0x17};
    std::copy(first.begin(), first.end(), cpsBytes.begin() + 33);
    std::copy(second.begin(), second.end(), cpsBytes.begin() + 64);
    VirtFile cpsRaw(cpsBytes.data(), cpsBytes.size(), "synthetic-cps.bin");
    CPS2Seq cps(&cpsRaw, 0, CPS2_V101, "synthetic CPS");
    check(cps.load(), "CPS parse");
    SequenceTimeline cpsTimeline; cps.captureEvents(cpsTimeline);
    const auto cpsPlan = planCPSSequence(cpsTimeline);
    check(cpsPlan.activeSlots == std::set<int>({0,9}) && cpsPlan.noteOns == 4, "CPS pointer slots and finite loop");
    check(cpsPlan.selection[0].program == 163 && cpsPlan.selection[1].bank == 1 && cpsPlan.selection[1].program == 1, "CPS opcodes 08 and 1f");
    unsigned executions = 0;
    for (auto& e : cpsTimeline.events) if (e.kind == K::NoteDuration && e.offset == 38) ++executions;
    check(cpsPlan.initialStates.at(9).volume == 77 && cpsPlan.initialStates.at(9).pan == 17, "CPS raw opcodes 07/18 through helpers");
    check(executions == 3, "loop execution values retained");
    std::cout << "Execution sink, NoItem, bank mapping, sparse slots, timing, pattern crossing, collisions and MIDI regression passed.\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
