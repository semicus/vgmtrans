/* VGMTrans (c) 2026. Licensed under the zlib license; see LICENSE. */
#include "ResourceTestRoot.h"
#include "CPS/CPSFurnaceAdapter.h"
#include "CPS/CPS2Instr.h"
#include "VGMRgn.h"
#include <array>
#include <iostream>
#include <stdexcept>

using namespace conversion;
void check(bool condition) { if (!condition) throw std::runtime_error("resource assertion failed"); }

int main() {
  try {
    ResourceTestRoot root;
    root.init();
    std::array<u8, 256> bytes{};
    bytes[16] = 7;  // Simulate an invalid original index mapped to sample 0.
    bytes[32] = 128; bytes[33] = 255; bytes[34] = 0; bytes[35] = 127;
    VirtFile raw(bytes.data(), bytes.size(), "synthetic.bin");
    CPS2SampleInfoTable table(&raw, "sample table", 0, 8);
    table.numSamples = 1;
    table.infos = new sample_info[1]{{32, 36, 96, 72}};
    CPS2InstrSet set(&raw, CPS2_V101, 0, 2, &table, nullptr, "synthetic instruments");
    auto* first = set.addInstr(0, 8, 0, 0);
    auto* region = first->addRgn(0, 8, 0);
    region->unityKey = 72;
    auto* second = set.addInstr(8, 8, 2, 0);
    second->addRgn(8, 8, 0)->unityKey = 72;
    auto* remapped = set.addInstr(16, 8, 0, 1);
    remapped->addRgn(16, 8, 0)->unityKey = 72;
    auto* upperHalf = set.addInstr(24, 8, 1, 35);  // Original bank 0 / program 163.
    upperHalf->addRgn(24, 8, 0)->unityKey = 72;
    VGMSampColl coll("CPS2", &raw, 0, bytes.size(), "synthetic PCM");
    auto* sample = coll.addSamp(32, 64, 32, 64, 1, BPS::PCM8, 24038);
    sample->unityKey = 72;
    sample->setLoopStatus(true); sample->setLoopOffset(4); sample->setLoopLength(60);
    const std::array<CPSInstrumentSelection, 2> selected{{{0, 0}, {1, 0}}};
    auto result = CPSFurnaceAdapter::convertResources(set, coll, selected);
    check(result.samples.size() == 1 && result.instruments.size() == 2);
    check(result.mappings[1].vgmBank == 2 && result.mappings[1].sample == 0);
    const auto converted = result.samples[0];
    check(converted.rate == 24038 && converted.c4Rate == 12019 && converted.rootKey == 72);
    check(converted.pcm[0] == -128 && converted.pcm[1] == -1 && converted.pcm[2] == 0 && converted.pcm[3] == 127);
    check(converted.loop->start == 4 && converted.loop->end == 64);
    check(sample->dataLength == 64 && region->unityKey == 72); // no source mutation
    const std::array<CPSInstrumentSelection, 1> upperSelection{{{0, 163}}};
    const auto upper = CPSFurnaceAdapter::convertResources(set, coll, upperSelection);
    check(upper.mappings[0].vgmBank == 1 && upper.mappings[0].vgmProgram == 35);
    int rejected = 0;
    auto rejectSelection = [&](std::span<const CPSInstrumentSelection> selection) {
      try { CPSFurnaceAdapter::convertResources(set, coll, selection); }
      catch (const std::invalid_argument&) { ++rejected; return; }
      throw std::runtime_error("invalid selection accepted");
    };
    rejectSelection({});
    const std::array<CPSInstrumentSelection, 2> duplicates{{{0, 0}, {0, 0}}};
    rejectSelection(duplicates);
    const std::array<CPSInstrumentSelection, 1> invalidBank{{{2, 0}}}, remappedSelection{{{0, 1}}};
    rejectSelection(invalidBank);
    rejectSelection(remappedSelection);
    auto reject = [&] {
      try { CPSFurnaceAdapter::convertResources(set, coll, selected); }
      catch (const std::invalid_argument&) { ++rejected; return; }
      throw std::runtime_error("invalid resource accepted");
    };
    region->sampNum = 1; reject(); region->sampNum = 0;
    region->fineTune = 1; reject(); region->fineTune = 0;
    sample->channels = 2; reject(); sample->channels = 1;
    sample->dataLength = 65; reject(); sample->dataLength = 64;
    sample->setLoopLength(61); reject(); sample->setLoopLength(60);
    sample->unityKey = 60; reject(); sample->unityKey = 72;
    sample->setRate(0); reject(); sample->setRate(24038);
    sample->setReverse(true); reject(); sample->setReverse(false);
    set.fmt_version = CPS2_V103; reject(); set.fmt_version = CPS2_V101;
    sample->dataOff = 33; reject(); sample->dataOff = 32;
    // Byte/frame equivalence for mono PCM8, and imported disabled loop policy.
    sample->setLoopStartMeasure(LM_SAMPLES); sample->setLoopLengthMeasure(LM_SAMPLES);
    check(CPSFurnaceAdapter::convertResources(set, coll, selected).samples[0].loop->end == 64);
    sample->setLoopStatus(false);
    reject();  // A long disabled loop is not the known importer heuristic.
    table.infos[0].loop_offset = 86;  // Imported short tail: 10 frames.
    result = CPSFurnaceAdapter::convertResources(set, coll, selected);
    check(!result.samples[0].loop && result.diagnostics.size() == 2);
    // Root-key calibration inputs: C4Rate changes by octaves without touching PCM.
    for (int key : {48, 60, 72}) {
      region->unityKey = key; second->regions()[0]->unityKey = key;
      sample->unityKey = key; table.infos[0].unity_key = key;
      result = CPSFurnaceAdapter::convertResources(set, coll, selected);
      check(result.samples[0].c4Rate == (key == 48 ? 48076 : key == 60 ? 24038 : 12019));
      check(result.samples[0].pcm == converted.pcm);
    }
    std::cout << "Resource mapping, PCM, root key, deduplication, loops and " << rejected << " rejections passed.\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
