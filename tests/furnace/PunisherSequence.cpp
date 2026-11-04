/* VGMTrans (c) 2026. Licensed under the zlib license; see LICENSE. */
#include "ResourceTestRoot.h"
#include "CPS/CPS2Seq.h"
#include "CPS/CPS2Instr.h"
#include "CPS/CPSFurnaceSequence.h"
#include "FurWriter.h"
#include "VGMColl.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>

using namespace conversion;
int main(int argc, char** argv) {
  try {
    if (argc < 3 || argc > 4) throw std::runtime_error("Usage: punisher_sequence ROM.zip --list | OUTPUT.fur COLLECTION_INDEX");
    ResourceTestRoot root; root.init();
    if (!root.openRawFile(argv[1])) throw std::runtime_error("Cannot load ROM");
    const bool list = std::string(argv[2]) == "--list";
    const int wanted = argc == 4 ? std::stoi(argv[3]) : -1;
    int index = -1;
    for (auto* coll : root.vgmColls()) {
      ++index;
      if (!list && index != wanted) continue;
      auto* seq = dynamic_cast<CPS2Seq*>(coll->seq());
      if (!seq || seq->fmt_version != CPS2_V101 || coll->instrSets().size() != 1 || coll->sampColls().size() != 1) continue;
      try {
        SequenceTimeline timeline;
        if (!seq->captureEvents(timeline)) throw std::runtime_error("Capture failed");
        auto plan = planCPSSequence(timeline);
        QSoundTimelineClock clock(timeline.events);
        auto* set = dynamic_cast<CPS2InstrSet*>(coll->instrSets()[0]);
        if (!set) throw std::runtime_error("Missing CPS instrument set");
        auto resources = CPSFurnaceAdapter::convertResources(*set, *coll->sampColls()[0], plan.selection);
        unsigned collisions = 0;
        std::vector<std::string> diagnostics;
        CPSControlReport controls;
        auto song = renderCPSSequence(plan, clock, resources, collisions, diagnostics, &controls);
        song.name = coll->name();
        song.comment = "Milestone 3B: raw CPS volume/pan; integrated tempo, 62.5 Hz, speed 1; no modulation.";
        std::ostringstream report;
        report << std::setprecision(12);
        report << "Collection " << index << ": " << coll->name() << " offset=" << seq->offset()
               << " slots=" << plan.activeSlots.size() << " on=" << plan.noteOns << " off=" << plan.noteOffs
               << " programs=" << plan.programs << " tempos=" << plan.tempos << " ticks=" << plan.endTick
               << " seconds=" << static_cast<double>(clock.seconds(plan.endTick)) << " endRow=" << clock.row(plan.endTick)
               << " collisions=" << collisions << " patterns/channel=" << song.channels[0].patterns.size() << '\n';
        auto printControl = [&](const char* name, const CPSControlStats& stats) {
          report << name << " executed=" << stats.executed << " written=" << stats.written
                 << " merged=" << stats.merged << " rejected=0 sourceRange=" << stats.sourceMin << ".." << stats.sourceMax
                 << " targetRange=" << stats.targetMin << ".." << stats.targetMax << '\n';
        };
        unsigned volumeCells = 0, panCells = 0;
        for (const auto& channel : song.channels) for (const auto& pattern : channel.patterns) for (const auto& cell : pattern.rows) {
          if (cell.volume) ++volumeCells;
          for (const auto& effect : cell.effects) if (effect.command == 0x80) ++panCells;
        }
        report << "Stored cells including initialization: volume=" << volumeCells << " pan80xx=" << panCells << '\n';
        printControl("Volume", controls.volume); printControl("Panning", controls.panning);
        report << "Volume mapping: round(255*CPSAmplitudeTable[raw]/8191), raw 0..127. Normalized channel gain, no MIDI CC; absolute hardware loudness not established without envelopes/master/DSP.\n"
               << "Pan mapping: ceil(255*raw/32), raw 0..32; 0->8000, 16->8080, 32->80FF; preserves QSound position index through v228 80xx conversion, not linear L/R gains. Default Furnace DSP unchanged.\n"
               << "Fallback initialization: volume raw127->255 and pan raw16->8080 on row0 of each musical channel; not counted as source writes. Explicit source controls override; intermediate sub-row states cannot be represented.\n";
        for (int slot : plan.activeSlots) report << "slot " << slot << " -> channel " << slot << '\n';
        for (auto& m : resources.mappings) {
          auto& s = resources.samples[m.sample];
          report << "bank=" << m.bank << " program=" << m.program << " sampNum=" << m.sampNum
                 << " instrument=" << m.instrument << " sample=" << m.sample << " root=" << m.rootKey
                 << " pcmOffset=" << coll->sampColls()[0]->sample(m.sampNum)->dataOff
                 << " rate=" << s.rate << " C4Rate=" << s.c4Rate << " length=" << s.pcm.size()
                 << " loop=" << (s.loop ? "[" + std::to_string(s.loop->start) + "," + std::to_string(s.loop->end) + ")" : "none") << '\n';
        }
        for (auto& e : timeline.events) if (e.kind == SequenceEvent::Kind::QSoundTempo)
          report << "tempo tick=" << e.tick << " T=" << e.value << " slot=" << e.slot << '\n';
        for (const auto* messages : {&plan.diagnostics, &resources.diagnostics, &diagnostics})
          for (auto& message : *messages) report << message << '\n';
        report << "Losses: envelopes, vibrato, portamento, pitch bend, echo/DSP and opcode 0x19 omitted.\n"
               << "Program/bank changes materialized at next Note On; no live instrument switching.\n"
               << "Timing: integral(deltaTick*256/(T*62.5)); round(absoluteSeconds*62.5). Driver accumulator phase unconfirmed; global transpose changes on held voices are not synthesized; <=8ms rounding error relative to integrated timeline.\n";
        if (list) {
          std::cout << report.str().substr(0, report.str().find('\n')) << " diagnostics=" << plan.diagnostics.size() << '\n';
          continue;
        }
        FurWriter::write(song, argv[2]);
        std::ofstream out(std::string(argv[2]) + ".txt"); out << report.str(); out.close();
        std::ofstream trace(std::string(argv[2]) + ".events.tsv");
        trace << "kind\ttick\tslot\tROMoffset\tvalue\tduration\torder\tdomain\n";
        for (auto& e : timeline.events) trace << static_cast<int>(e.kind) << '\t' << e.tick << '\t' << e.slot << '\t' << e.offset << '\t' << e.value << '\t' << e.duration << '\t' << e.order << '\t' << static_cast<int>(e.domain) << '\n';
        trace.close();
        if (!out || !trace) throw std::runtime_error("Cannot write diagnostics");
        std::cout << report.str() << std::filesystem::absolute(argv[2]).lexically_normal() << '\n';
        return 0;
      } catch (const std::exception& e) {
        if (!list) throw;
        std::cout << "Collection " << index << ": " << coll->name() << " rejected: " << e.what() << '\n';
      }
    }
    if (!list) throw std::runtime_error("Collection not found");
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
