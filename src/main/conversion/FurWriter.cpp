/*
 * VGMTrans (c) 2026
 * Licensed under the zlib license; see LICENSE.
 *
 * Interoperability reference: Furnace tag v0.6.8.1, papers/format.md and
 * papers/newIns.md. This serializer is independently implemented.
 */
#include "FurWriter.h"

#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace conversion {
namespace {
using namespace module;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::invalid_argument("Furnace v228: " + message);
}

void checkText(const std::string& text) {
  require(text.find('\0') == std::string::npos, "text contains an embedded NUL");
  // Validate UTF-8, including overlong sequences, surrogates and U+10FFFF.
  for (std::size_t i = 0; i < text.size();) {
    auto first = static_cast<unsigned char>(text[i++]);
    if (first < 0x80) continue;
    int count = first >= 0xc2 && first <= 0xdf ? 1 :
                first >= 0xe0 && first <= 0xef ? 2 :
                first >= 0xf0 && first <= 0xf4 ? 3 : -1;
    require(count > 0 && text.size() - i >= static_cast<std::size_t>(count), "invalid UTF-8");
    std::uint32_t code = first & ((1u << (6 - count)) - 1);
    for (int j = 0; j < count; ++j) {
      auto next = static_cast<unsigned char>(text[i++]);
      require((next & 0xc0) == 0x80, "invalid UTF-8 continuation");
      code = (code << 6) | (next & 0x3f);
    }
    const std::uint32_t minimum[] = {0, 0x80, 0x800, 0x10000};
    require(code >= minimum[count] && code <= 0x10ffff &&
            !(code >= 0xd800 && code <= 0xdfff), "invalid UTF-8 code point");
  }
}

void validate(const Song& song) {
  checkText(song.name);
  checkText(song.author);
  checkText(song.comment);
  require(!song.systems.empty() && song.systems.size() <= 32, "expected 1..32 systems");
  for (const auto& system : song.systems) {
    require(system.chip == Chip::QSound, "unsupported target chip");
    require(std::isfinite(system.volume) && system.volume >= 0 && system.volume <= 2,
            "chip volume must be finite and in 0..2");
    require(std::isfinite(system.pan) && system.pan >= -1 && system.pan <= 1,
            "chip pan must be finite and in -1..1");
  }
  require(song.channels.size() == song.systems.size() * 19 && song.channels.size() <= 128,
          "QSound requires 19 channels per chip, at most 128 total in this profile");
  require(std::isfinite(song.timing.ticksPerSecond) && song.timing.ticksPerSecond > 0 &&
          song.timing.ticksPerSecond <= 1000, "tick rate must be in (0, 1000]");
  require(song.timing.speed > 0, "speed must be nonzero");
  require(song.timing.rowsPerPattern >= 1 && song.timing.rowsPerPattern <= 256,
          "pattern length must be 1..256");
  require(song.instruments.size() <= 256 && song.samples.size() <= 256,
          "at most 256 instruments and 256 samples are supported");
  for (const auto& sample : song.samples) {
    checkText(sample.name);
    // The stable QSound renderer reserves 16 bytes per 64 KiB bank.
    require(!sample.pcm.empty() && sample.pcm.size() <= 65520, "PCM length must be 1..65520");
    require(sample.rate > 0 && sample.rate <= 384000 && sample.c4Rate > 0 &&
            sample.c4Rate <= 384000, "sample rates must be 1..384000 Hz");
    if (sample.loop) {
      require(sample.loop->start < sample.loop->end && sample.loop->end <= sample.pcm.size(),
              "invalid sample loop bounds");
      require(sample.loop->end - sample.loop->start <= 32767, "QSound loop exceeds 32767 frames");
    }
  }
  for (const auto& instrument : song.instruments) {
    checkText(instrument.name);
    require(instrument.name.size() < 65535, "instrument name exceeds feature length");
    require(instrument.type == InstrumentType::QSound, "unsupported instrument type");
    require(instrument.sample < song.samples.size(), "instrument references missing sample");
  }
  const auto orders = song.channels.front().orders.size();
  require(orders >= 1 && orders <= 256, "order count must be 1..256");
  for (const auto& channel : song.channels) {
    checkText(channel.name);
    checkText(channel.shortName);
    require(channel.orders.size() == orders, "all channels must have equal order counts");
    require(channel.effectColumns >= 1 && channel.effectColumns <= 8, "effect columns must be 1..8");
    require(channel.patterns.size() <= 256, "too many patterns in channel");
    std::set<std::uint16_t> indices;
    for (const auto& pattern : channel.patterns) {
      checkText(pattern.name);
      require(pattern.index <= 255 && indices.insert(pattern.index).second,
              "duplicate or out-of-range pattern index");
      require(pattern.rows.size() <= song.timing.rowsPerPattern, "pattern has too many rows");
      for (const auto& cell : pattern.rows) {
        if (cell.note) {
          require(cell.note->kind == Note::Kind::Pitch || cell.note->kind == Note::Kind::Off,
                  "unsupported note kind");
          if (cell.note->kind == Note::Kind::Pitch)
            require(cell.note->semitone >= -60 && cell.note->semitone <= 119, "note outside C-(-5)..B-9");
        }
        if (cell.instrument) require(*cell.instrument < song.instruments.size(), "missing instrument");
        require(cell.effects.size() <= channel.effectColumns, "too many effects in row");
      }
    }
    for (auto order : channel.orders)
      require(indices.contains(order), "order references missing pattern");
  }
}

class Bytes {
public:
  std::vector<std::uint8_t> data;

  std::uint32_t offset() const {
    if (data.size() > std::numeric_limits<std::uint32_t>::max())
      throw std::length_error("Furnace file exceeds 32-bit offsets");
    return static_cast<std::uint32_t>(data.size());
  }
  void u8(std::uint8_t value) { data.push_back(value); }
  void u16(std::uint16_t value) {
    u8(static_cast<std::uint8_t>(value));
    u8(static_cast<std::uint8_t>(value >> 8));
  }
  void u32(std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) u8(static_cast<std::uint8_t>(value >> shift));
  }
  void f32(float value) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    u32(std::bit_cast<std::uint32_t>(value));
  }
  void raw(std::string_view value) { data.insert(data.end(), value.begin(), value.end()); }
  void text(std::string_view value) { raw(value); u8(0); }
  void zeros(std::size_t count) { data.insert(data.end(), count, 0); }
  std::uint32_t pointer() { auto pos = offset(); u32(0); return pos; }
  void patch(std::uint32_t pos, std::uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte)
      data.at(static_cast<std::size_t>(pos) + byte) = static_cast<std::uint8_t>(value >> (byte * 8));
  }
  std::uint32_t block(std::string_view id) { raw(id); return pointer(); }
  void end(std::uint32_t sizeField) { patch(sizeField, offset() - sizeField - 4); }
  void pointHere(std::uint32_t pos) { patch(pos, offset()); }
};

void writeCell(Bytes& out, const Cell& cell) {
  std::uint16_t effectMask = 0;
  for (std::size_t i = 0; i < cell.effects.size(); ++i) {
    if (cell.effects[i].command) effectMask |= static_cast<std::uint16_t>(1u << (2 * i));
    if (cell.effects[i].value) effectMask |= static_cast<std::uint16_t>(2u << (2 * i));
  }
  std::uint8_t mask = (cell.note ? 1 : 0) | (cell.instrument ? 2 : 0) | (cell.volume ? 4 : 0);
  if (effectMask & 0xff) mask |= 0x20;
  if (effectMask & 0xff00) mask |= 0x40;
  out.u8(mask);
  if (mask & 0x20) out.u8(static_cast<std::uint8_t>(effectMask));
  if (mask & 0x40) out.u8(static_cast<std::uint8_t>(effectMask >> 8));
  if (cell.note) out.u8(cell.note->kind == Note::Kind::Off ? 180 :
                        static_cast<std::uint8_t>(cell.note->semitone + 60));
  if (cell.instrument) out.u8(static_cast<std::uint8_t>(*cell.instrument));
  if (cell.volume) out.u8(*cell.volume);
  for (const auto& effect : cell.effects) {
    if (effect.command) out.u8(*effect.command);
    if (effect.value) out.u8(*effect.value);
  }
}
}  // namespace

std::vector<std::uint8_t> FurWriter::serialize(const module::Song& song) {
  validate(song);
  Bytes out;
  out.raw("-Furnace module-");  // Exactly 16 bytes; no string terminator.
  out.u16(version); out.u16(0);
  const auto infoPointer = out.pointer();
  out.zeros(8);
  out.pointHere(infoPointer);
  const auto info = out.block("INFO");
  out.u8(0);  // time base: no multiplier
  out.u8(song.timing.speed); out.u8(song.timing.speed); out.u8(1);
  out.f32(song.timing.ticksPerSecond);
  out.u16(song.timing.rowsPerPattern);
  out.u16(static_cast<std::uint16_t>(song.channels.front().orders.size()));
  out.u8(4); out.u8(16);  // editor highlights only
  out.u16(static_cast<std::uint16_t>(song.instruments.size())); out.u16(0);  // no waves
  out.u16(static_cast<std::uint16_t>(song.samples.size()));
  std::uint32_t patternCount = 0;
  for (const auto& channel : song.channels) patternCount += static_cast<std::uint32_t>(channel.patterns.size());
  out.u32(patternCount);
  for (std::size_t i = 0; i < 32; ++i) out.u8(i < song.systems.size() ? 0xe0 : 0);
  out.zeros(64);  // obsolete chip volume and panning arrays
  std::vector<std::uint32_t> flagPointers;
  for (std::size_t i = 0; i < 32; ++i) flagPointers.push_back(out.pointer());
  out.text(song.name); out.text(song.author); out.f32(440.0f);
  // v228 basic compatibility flags: no legacy emulation, non-linear pitch.
  // No pitch effects are generated by this milestone.
  out.zeros(20);
  std::vector<std::uint32_t> instrumentPointers, samplePointers, patternPointers;
  for (std::size_t i = 0; i < song.instruments.size(); ++i) instrumentPointers.push_back(out.pointer());
  for (std::size_t i = 0; i < song.samples.size(); ++i) samplePointers.push_back(out.pointer());
  for (std::uint32_t i = 0; i < patternCount; ++i) patternPointers.push_back(out.pointer());
  for (const auto& channel : song.channels)
    for (auto order : channel.orders) out.u8(static_cast<std::uint8_t>(order));
  for (const auto& channel : song.channels) out.u8(channel.effectColumns);
  for (std::size_t i = 0; i < song.channels.size(); ++i) out.u8(3);  // show channel and oscilloscope
  out.zeros(song.channels.size());  // not collapsed
  for (const auto& channel : song.channels) out.text(channel.name);
  for (const auto& channel : song.channels) out.text(channel.shortName);
  out.text(song.comment); out.f32(1.0f);  // master gain
  // Extended compatibility fields, in v228 order. All are zero except modern
  // volume scaling (index 16). No legacy effects or automatic system naming.
  for (int i = 0; i < 28; ++i) out.u8(i == 16 ? 1 : 0);
  out.u16(1); out.u16(1);  // virtual tempo
  out.text(""); out.text(""); out.zeros(4);  // first subsong metadata, no additional subsongs
  for (int i = 0; i < 6; ++i) out.text("");  // additional metadata
  for (const auto& system : song.systems) {
    out.f32(system.volume); out.f32(system.pan); out.f32(0.0f);
  }
  // Explicit stereo routing, with automatic routing enabled for future edits.
  out.u32(static_cast<std::uint32_t>(song.systems.size() * 2));
  for (std::uint32_t i = 0; i < song.systems.size(); ++i) {
    out.u32((i * 16) << 16);
    out.u32(((i * 16 + 1) << 16) | 1);
  }
  out.u8(1);
  out.zeros(8);  // remaining compatibility flags through v200
  out.u8(1); out.u8(song.timing.speed); out.zeros(15);  // one-entry speed pattern
  out.u8(0);  // no grooves
  std::array<std::uint32_t, 3> directoryPointers{};
  for (auto& pointer : directoryPointers) pointer = out.pointer();
  out.end(info);

  for (std::size_t i = 0; i < song.systems.size(); ++i) {
    out.pointHere(flagPointers[i]);
    const auto block = out.block("FLAG");
    out.text("");  // chip defaults; no echo/spatial effect programming
    out.end(block);
  }
  // v228's loader reads all three directories even when they contain no assets.
  for (auto pointer : directoryPointers) {
    out.pointHere(pointer);
    const auto block = out.block("ADIR"); out.u32(0); out.end(block);
  }
  for (std::size_t i = 0; i < song.instruments.size(); ++i) {
    const auto& instrument = song.instruments[i];
    out.pointHere(instrumentPointers[i]);
    const auto block = out.block("INS2");
    out.u16(version); out.u16(40);  // QSound target instrument
    out.raw("NA"); out.u16(static_cast<std::uint16_t>(instrument.name.size() + 1));
    out.text(instrument.name);
    out.raw("SM"); out.u16(4); out.u16(instrument.sample); out.u8(2); out.u8(0);
    out.raw("EN");  // terminator has no length or payload
    out.end(block);
  }
  for (std::size_t i = 0; i < song.samples.size(); ++i) {
    const auto& sample = song.samples[i];
    out.pointHere(samplePointers[i]);
    const auto block = out.block("SMP2");
    out.text(sample.name); out.u32(static_cast<std::uint32_t>(sample.pcm.size()));
    out.u32(sample.rate); out.u32(sample.c4Rate);
    out.u8(8); out.zeros(3);  // PCM8, forward loop, no BRR/dither flags
    out.u32(sample.loop ? sample.loop->start : 0xffffffffu);
    out.u32(sample.loop ? sample.loop->end : 0xffffffffu);
    out.u32((1u << song.systems.size()) - 1); out.zeros(12);  // PCM bank, all configured chips
    for (auto frame : sample.pcm) out.u8(static_cast<std::uint8_t>(frame));
    out.end(block);
  }
  std::size_t patternOrdinal = 0;
  for (std::size_t channelIndex = 0; channelIndex < song.channels.size(); ++channelIndex) {
    for (const auto& pattern : song.channels[channelIndex].patterns) {
      out.pointHere(patternPointers[patternOrdinal++]);
      const auto block = out.block("PATN");
      out.u8(0); out.u8(static_cast<std::uint8_t>(channelIndex)); out.u16(pattern.index);
      out.text(pattern.name);
      for (const auto& cell : pattern.rows) writeCell(out, cell);
      if (pattern.rows.size() < song.timing.rowsPerPattern) out.u8(0xff);
      out.end(block);
    }
  }
  out.offset();  // check final size as well as all referenced offsets
  return std::move(out.data);
}

void FurWriter::write(const module::Song& song, const std::filesystem::path& path) {
  const auto bytes = serialize(song);
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) throw std::runtime_error("Cannot open Furnace output: " + path.string());
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  stream.close();
  if (!stream) throw std::runtime_error("Cannot write Furnace output: " + path.string());
}
}  // namespace conversion
