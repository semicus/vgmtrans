/* VGMTrans (c) 2026. Licensed under the zlib license; see LICENSE. */
#pragma once
#include "ModuleModel.h"
#include <span>

class CPS2InstrSet;
class VGMSampColl;

namespace conversion {
struct CPSInstrumentSelection {
  unsigned bank;     // Original V1.01 bank: 0..1 (not the MIDI bank).
  unsigned program;  // Original program: 0..255.
};
struct CPSResourceMapping {
  unsigned bank, program, vgmBank, vgmProgram, sampNum;
  int rootKey;
  std::uint16_t instrument, sample;
};
struct CPSFurnaceResources {
  std::vector<module::Sample> samples;
  std::vector<module::Instrument> instruments;
  std::vector<CPSResourceMapping> mappings;
  std::vector<std::string> diagnostics;
};

// Resource-only adapter. Does not read or execute a music sequence.
class CPSFurnaceAdapter {
public:
  static CPSFurnaceResources convertResources(
      const CPS2InstrSet& instruments, const VGMSampColl& samples,
      std::span<const CPSInstrumentSelection> selection);
};
}  // namespace conversion
