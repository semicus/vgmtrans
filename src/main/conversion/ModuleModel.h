/*
 * VGMTrans (c) 2026
 * Licensed under the zlib license; see LICENSE.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace conversion::module {

// Target capabilities, not source formats. Additional chips can be added without
// teaching the model or writer anything about their source sequence parsers.
enum class Chip { QSound };
enum class InstrumentType { QSound };

struct System {
  Chip chip = Chip::QSound;
  float volume = 1.0f;
  float pan = 0.0f;  // -1 left, +1 right
};

struct Timing {
  float ticksPerSecond = 62.5f;
  std::uint8_t speed = 1;
  std::uint16_t rowsPerPattern = 64;
};

struct Note {
  enum class Kind { Pitch, Off };
  Kind kind = Kind::Pitch;
  // Chromatic semitones relative to C-0; C-4 = 48. Not a MIDI key.
  int semitone = 48;
};

struct Effect {
  // Independent presence permits effect memory and value-only cells.
  std::optional<std::uint8_t> command;
  std::optional<std::uint8_t> value;
};

struct Cell {
  std::optional<Note> note;
  std::optional<std::uint16_t> instrument;
  std::optional<std::uint8_t> volume;
  std::vector<Effect> effects;  // Position is the effect-column index.
};

struct Pattern {
  std::uint16_t index = 0;  // Local to its channel, not a global pattern ID.
  std::string name;
  std::vector<Cell> rows;  // Omitted trailing rows are empty.
};

struct Channel {
  std::string name;
  std::string shortName;
  std::uint8_t effectColumns = 1;
  std::vector<std::uint16_t> orders;
  std::vector<Pattern> patterns;
};

struct SampleLoop {
  std::uint32_t start = 0;
  std::uint32_t end = 0;  // Exclusive, in PCM frames (one byte for mono PCM8).
};

struct Sample {
  std::string name;
  std::vector<std::int8_t> pcm;  // Mono, signed PCM8; never WAV unsigned PCM8.
  std::uint32_t rate = 24038;
  std::uint32_t c4Rate = 24038;  // Playback rate at the model's C-4.
  std::optional<SampleLoop> loop;
};

struct Instrument {
  std::string name;
  InstrumentType type = InstrumentType::QSound;
  std::uint16_t sample = 0;
};

struct Song {
  std::string name;
  std::string author;
  std::string comment;
  Timing timing;
  std::vector<System> systems;
  // Channels are concatenated in system order. QSound contributes 19 channels.
  std::vector<Channel> channels;
  std::vector<Instrument> instruments;
  std::vector<Sample> samples;
};

}  // namespace conversion::module
