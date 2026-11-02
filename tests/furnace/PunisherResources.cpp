/* VGMTrans (c) 2026. Licensed under the zlib license; see LICENSE. */
#include "ResourceTestRoot.h"
#include "CPS/CPSFurnaceAdapter.h"
#include "CPS/CPS2Instr.h"
#include "FurWriter.h"
#include "VGMColl.h"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace conversion;

int main(int argc, char** argv) {
  try {
    if (argc != 3) throw std::runtime_error("Usage: punisher_resources ROM.zip OUTPUT.fur (or --list)");
    ResourceTestRoot root;
    root.init();
    if (!root.openRawFile(argv[1])) throw std::runtime_error("ROM load failed");
    const VGMColl* collection = nullptr;
    CPS2InstrSet* set = nullptr;
    for (auto* coll : root.vgmColls()) {
      if (coll->instrSets().size() != 1 || coll->sampColls().size() != 1) continue;
      auto* candidate = dynamic_cast<CPS2InstrSet*>(coll->instrSets()[0]);
      if (candidate && candidate->fmt_version == CPS2_V101 && coll->name().find("punisher") != std::string::npos) {
        collection = coll; set = candidate; break;
      }
    }
    if (!collection) throw std::runtime_error("No Punisher V1.01 collection found");
    std::cout << "Collection: " << collection->name() << "\n";
    if (std::string(argv[2]) == "--list") {
      for (unsigned bank = 0; bank < 2; ++bank) for (unsigned program = 0; program < 256; ++program) {
        try {
          const std::array<CPSInstrumentSelection, 1> selection{{{bank, program}}};
          const auto r = CPSFurnaceAdapter::convertResources(*set, *collection->sampColls()[0], selection);
          const auto& s = r.samples[0];
          std::cout << bank << ':' << program << " samp=" << r.mappings[0].sampNum
                    << " root=" << *s.rootKey << " length=" << s.pcm.size()
                    << " loop=" << (s.loop ? std::to_string(s.loop->start) : "none") << '\n';
        } catch (const std::invalid_argument& error) {
          std::cerr << bank << ':' << program << " rejected: " << error.what() << '\n';
        }
      }
      return 0;
    }
    const std::array<CPSInstrumentSelection, 3> selection{{{0, 0}, {0, 163}, {1, 1}}};
    auto resources = CPSFurnaceAdapter::convertResources(*set, *collection->sampColls()[0], selection);
    module::Song song;
    song.name = "The Punisher - resource test";
    song.comment = "Manual C-4/C-5/C-3 for selected resources; no source sequence export.";
    song.systems.push_back({});
    song.timing.rowsPerPattern = 128;
    song.channels.resize(19);
    for (auto& ch : song.channels) {
      for (std::uint16_t i = 0; i < resources.instruments.size(); ++i) {
        ch.orders.push_back(i); ch.patterns.push_back({i, "", {}});
      }
    }
    song.samples = resources.samples;
    song.instruments = resources.instruments;
    for (std::uint16_t ins = 0; ins < song.instruments.size(); ++ins) {
      auto& rows = song.channels[0].patterns[ins].rows;
      rows.resize(128);
      const int notes[] = {48, 60, 36};
      for (int i = 0; i < 3; ++i) {
        const auto row = i * 40;
        rows[row].note = module::Note{module::Note::Kind::Pitch, notes[i]};
        rows[row].instrument = ins;
        rows[row].volume = 128;
        rows[row + 32].note = module::Note{module::Note::Kind::Off, 0};
      }
    }
    song.channels[0].patterns.back().rows[127].effects = {{0xff, 0}};
    const std::filesystem::path output = argv[2];
    FurWriter::write(song, output);
    std::ofstream report(output.string() + ".txt");
    if (!report) throw std::runtime_error("Cannot write diagnostic report");
    for (const auto& m : resources.mappings) {
      const auto& s = resources.samples[m.sample];
      const auto* original = collection->sampColls()[0]->sample(m.sampNum);
      const auto line = "bank=" + std::to_string(m.bank) + " program=" + std::to_string(m.program) +
          " vgmBank=" + std::to_string(m.vgmBank) + " vgmProgram=" + std::to_string(m.vgmProgram) +
          " sampNum=" + std::to_string(m.sampNum) + " root=" + std::to_string(m.rootKey) +
          " instrument=" + std::to_string(m.instrument) + " sample=" + std::to_string(m.sample) +
          " rate=" + std::to_string(s.rate) + " C4Rate=" + std::to_string(s.c4Rate) +
          " length=" + std::to_string(s.pcm.size()) + " pcmOffset=" + std::to_string(original->dataOff) + " loop=" +
          (s.loop ? "[" + std::to_string(s.loop->start) + "," + std::to_string(s.loop->end) + ")" : "none");
      report << line << '\n'; std::cout << line << '\n';
    }
    for (const auto& diagnostic : resources.diagnostics) { report << diagnostic << '\n'; std::cout << diagnostic << '\n'; }
    report.close();
    if (!report) throw std::runtime_error("Cannot finish diagnostic report");
    std::cout << std::filesystem::absolute(output) << '\n';
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
