/* VGMTrans (c) 2026. Licensed under the zlib license; see LICENSE. */
#include "CPSFurnaceAdapter.h"
#include "CPS2Instr.h"
#include "VGMRgn.h"
#include <bit>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace conversion {
namespace {
void require(bool value, const char* message) {
  if (!value) throw std::invalid_argument(std::string("CPS Furnace resources: ") + message);
}
}
CPSFurnaceResources CPSFurnaceAdapter::convertResources(
    const CPS2InstrSet& set, const VGMSampColl& coll,
    std::span<const CPSInstrumentSelection> selection) {
  require(set.fmt_version == CPS2_V101, "only CPS2_V1.01 is supported");
  require(!selection.empty() && selection.size() <= 256, "select 1..256 instruments explicitly");
  require(set.sampInfoTable && set.sampInfoTable->infos && set.sampInfoTable->numSamples,
          "missing sample info table");
  CPSFurnaceResources result;
  std::map<std::pair<unsigned, int>, std::uint16_t> sampleMap;
  std::set<std::pair<unsigned, unsigned>> selected;
  for (const auto& request : selection) {
    require(request.bank < 2 && request.bank < set.num_instr_banks && request.program < 256,
            "source bank/program out of range");
    require(selected.emplace(request.bank, request.program).second, "duplicate selection");
    const unsigned bank = request.bank * 2 + request.program / 128;
    const unsigned program = request.program % 128;
    VGMInstr* source = nullptr;
    for (auto* instrument : set.instrs()) {
      if (instrument->bank == bank && instrument->instrNum == program) {
        require(!source, "ambiguous bank/program");
        source = instrument;
      }
    }
    require(source && source->regions().size() == 1, "expected one instrument region");
    const auto& region = *source->regions()[0];
    require(region.sampCollPtr == nullptr || region.sampCollPtr == &coll, "wrong region sample collection");
    require(region.sampOffset == -1 && region.loop.loopStatus == -1, "unsupported region overrides");
    require(region.fineTune == 0 && region.coarseTune == 0, "unexpected tuning in V1.01 region");
    require(region.sampNum < coll.sampleCount() && region.sampNum < set.sampInfoTable->numSamples,
            "missing sample");
    // V1.01 stores its sample index in the first byte. Detect the importer's
    // invalid-index-to-zero fallback instead of silently exporting another sound.
    require(source->rawFile() && source->offset() < source->rawFile()->size(), "invalid instrument offset");
    require(source->rawFile()->readByte(source->offset()) == region.sampNum, "sample index was remapped by importer");
    auto& sample = *coll.sample(region.sampNum);
    const auto& info = set.sampInfoTable->infos[region.sampNum];
    const auto base = set.sampInfoTable->infos[0].start_addr & 0xff0000;
    require(info.start_addr >= base && sample.dataOff == info.start_addr - base,
            "sample collection does not match instrument table");
    require(info.end_addr >= info.start_addr && info.end_addr - info.start_addr > 2,
            "empty/short source sample was padded by importer");
    require(sample.dataLength == info.end_addr - info.start_addr, "sample length was changed by importer");
    require(sample.channels == 1 && sample.bps() == BPS::PCM8 &&
            sample.signedness() == Signedness::Signed && !sample.reverse(), "expected native mono signed PCM8");
    require(sample.rawFile() && sample.dataOff <= sample.rawFile()->size() &&
            sample.dataLength <= sample.rawFile()->size() - sample.dataOff, "PCM outside source file");
    require(sample.rate > 0 && sample.rate <= 384000 && sample.fineTune == 0, "invalid sample tuning");
    require(region.unityKey >= 0 && region.unityKey == sample.unityKey && region.unityKey == info.unity_key,
            "inconsistent/missing root key");
    const int root = region.unityKey;
    const auto key = std::make_pair(region.sampNum, root);
    auto found = sampleMap.find(key);
    std::uint16_t sampleIndex;
    if (found == sampleMap.end()) {
      module::Sample converted;
      converted.name = "QSound sample " + std::to_string(region.sampNum) + " root " + std::to_string(root);
      const auto pcm = sample.toPcm(Signedness::Signed, Endianness::Little, BPS::PCM8);
      require(pcm.size() == sample.dataLength && pcm.size() <= 65520, "unsupported PCM length");
      for (auto byte : pcm) converted.pcm.push_back(std::bit_cast<std::int8_t>(byte));
      converted.rate = sample.rate;
      const double c4Rate = sample.rate * std::exp2((60.0 - root) / 12.0);
      require(c4Rate >= 1 && c4Rate <= 384000, "C4Rate outside writer profile");
      converted.c4Rate = static_cast<std::uint32_t>(std::lround(c4Rate));
      converted.rootKey = root;
      require(sample.loopStatus() == 0 || sample.loopStatus() == 1, "unknown loop status");
      if (sample.loopStatus()) {
        require(sample.loop.loopType == 0, "only forward loops are supported");
        require((sample.loop.loopStartMeasure == LM_BYTES || sample.loop.loopStartMeasure == LM_SAMPLES) &&
                (sample.loop.loopLengthMeasure == LM_BYTES || sample.loop.loopLengthMeasure == LM_SAMPLES),
                "unknown loop units");
        // Mono PCM8: a byte and a frame have the same size. Addition is widened.
        const std::uint64_t end = std::uint64_t(sample.loop.loopStart) + sample.loop.loopLength;
        require(sample.loop.loopLength > 0 && sample.loop.loopLength <= 32767 && end <= pcm.size(),
                "invalid/unsupported loop bounds");
        require(info.loop_offset >= info.start_addr &&
                sample.loop.loopStart == info.loop_offset - info.start_addr && end == pcm.size(),
                "imported loop differs from source sample table");
        converted.loop = module::SampleLoop{sample.loop.loopStart, static_cast<std::uint32_t>(end)};
      } else if (info.loop_offset < info.end_addr) {
        require(info.loop_offset >= info.start_addr && info.end_addr - info.loop_offset < 40,
                "unexpected disabled loop (not the importer's short-tail heuristic)");
        result.diagnostics.push_back("Sample " + std::to_string(region.sampNum) +
            ": importer disabled source tail loop (" + std::to_string(info.end_addr - info.loop_offset) + " frames)");
      }
      sampleIndex = static_cast<std::uint16_t>(result.samples.size());
      sampleMap.emplace(key, sampleIndex);
      result.samples.push_back(std::move(converted));
    } else sampleIndex = found->second;
    const auto instrumentIndex = static_cast<std::uint16_t>(result.instruments.size());
    result.instruments.push_back({"CPS bank " + std::to_string(request.bank) + " program " +
        std::to_string(request.program), module::InstrumentType::QSound, sampleIndex});
    result.mappings.push_back({request.bank, request.program, bank, program, region.sampNum, root,
                               instrumentIndex, sampleIndex});
  }
  result.diagnostics.push_back("V1.01 has no fine-tune field: neutral. ADSR is intentionally omitted.");
  return result;
}
}  // namespace conversion
