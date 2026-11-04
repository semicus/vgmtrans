/* VGMTrans (c) 2026. Licensed under the zlib license; see LICENSE. */
#include "CPS/CPSFurnaceSequence.h"
#include "FurWriter.h"
#include <iostream>
#include <stdexcept>
using namespace conversion;
using K = SequenceEvent::Kind;
void check(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
int main(int argc, char** argv) {
  try {
    check(argc == 2, "output directory required");
    check(cpsVolumeToFurnace(0) == 0 && cpsVolumeToFurnace(127) == 255, "volume endpoints");
    check(cpsVolumeToFurnace(110) == 108 && cpsVolumeToFurnace(77) == 51, "nonlinear volume");
    for (int i = 1; i < 128; ++i) check(cpsVolumeToFurnace(i) >= cpsVolumeToFurnace(i-1), "monotonic volume");
    check(cpsPanToFurnace(0) == 0 && cpsPanToFurnace(16) == 128 && cpsPanToFurnace(32) == 255, "pan endpoints/center");
    check(cpsPanToFurnace(8) == 64 && cpsPanToFurnace(17) == 136 && cpsPanToFurnace(24) == 192, "intermediate pan");
    for (int i = 0; i <= 32; ++i) check(cpsPanToFurnace(i)*32/255 == i, "all 33 target positions");
    for (int bad : {-1,128}) {
      bool rejected = false;
      try { cpsVolumeToFurnace(bad); } catch (const std::invalid_argument&) { rejected = true; }
      check(rejected, "invalid volume");
    }
    for (int bad : {-1,33}) {
      bool rejected = false;
      try { cpsPanToFurnace(bad); } catch (const std::invalid_argument&) { rejected = true; }
      check(rejected, "invalid pan");
    }
    SequenceTimeline timeline;
    auto event = [&](K kind, unsigned tick, int value, unsigned duration = 0) {
      timeline.events.push_back({kind,tick,2,100,value,duration,
        (kind == K::Volume || kind == K::Panning) ? SequenceEvent::ValueDomain::SourceRaw : SequenceEvent::ValueDomain::NativeHelper});
    };
    event(K::QSoundTempo,0,1024); event(K::Bank,0,0); event(K::Program,0,0);
    event(K::Volume,0,0); event(K::Panning,0,0);
    event(K::NoteDuration,3,60,10);
    event(K::Volume,4,110); event(K::Panning,4,16); // held note / same row
    event(K::Volume,8,100); event(K::Panning,8,32);
    event(K::Volume,9,127); event(K::Panning,9,8); // merge, final state wins
    event(K::NoteDuration,12,64,8);
    for (int raw : {32,64,127}) event(K::Volume,12,raw);
    for (int raw : {8,16,32}) event(K::Panning,12,raw);
    event(K::Program,16,1); event(K::NoteDuration,16,67,2); // level persists across instrument
    event(K::Panning,1024,0); event(K::End,1032,0);
    const auto plan = planCPSSequence(timeline);
    check(plan.initialStates.at(2).volume == 0 && plan.initialStates.at(2).pan == 0, "before first note state");
    QSoundTimelineClock clock(timeline.events);
    CPSFurnaceResources resources;
    resources.samples.push_back({"PCM", {0,127,-128,0},24038,24038,{},{}});
    resources.instruments = {{"first",module::InstrumentType::QSound,0},{"second",module::InstrumentType::QSound,0}};
    resources.mappings = {{0,0,0,0,0,60,0,0},{0,1,0,1,0,60,1,0}};
    unsigned collisions; std::vector<std::string> diagnostics; CPSControlReport report;
    auto song = renderCPSSequence(plan, clock, resources, collisions, diagnostics, &report);
    const auto& rows = song.channels[2].patterns[0].rows;
    check(!rows[0].note && rows[0].volume == 0 && rows[0].effects[0].value == 0, "control-only before first note");
    check(rows[1].note && rows[1].volume == 108 && rows[1].effects[0].value == 128, "held note/control row");
    check(!rows[2].note && rows[2].volume == 255 && rows[2].effects[0].value == 64, "multiple controls final state");
    check(rows[3].note && rows[3].instrument == 0 && rows[3].volume == 255 && rows[3].effects[0].value == 255, "note volume pan coexist");
    check(rows[4].instrument == 1 && !rows[4].volume && rows[4].effects.empty(), "program does not reset controls");
    check(song.channels[2].patterns[1].rows[0].effects[0].value == 0, "control crosses pattern boundary");
    check(report.volume.executed == 7 && report.volume.written == 4 && report.volume.merged == 3, "volume stats");
    check(report.panning.executed == 8 && report.panning.written == 5 && report.panning.merged == 3, "pan stats");
    check(collisions == 0, "controls never count as note collisions");
    // Verify multiple effect columns serialize without overwriting the Stop.
    song.channels[0].patterns[1].rows[3].effects.push_back({0x80,128});
    song.channels[0].effectColumns = 2;
    std::filesystem::create_directories(argv[1]);
    FurWriter::write(song, std::filesystem::path(argv[1]) / "controls-v228.fur");
    std::cout << "Raw controls, state persistence, quantization, intermediate/endpoints and writer fixture passed.\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
