/* VGMTrans (c) 2026. Licensed under the zlib license; see LICENSE. */
#include "FurWriter.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>

using namespace conversion;
using namespace conversion::module;
namespace fs = std::filesystem;

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

Song syntheticSong() {
  Song song;
  song.name = "Synthetic QSound C-4";
  song.author = "VGMTrans";
  song.comment = "Independent v228 writer test: C-4 at row 0, off at 32, stop at 63.";
  song.systems.push_back({});
  song.channels.resize(19);
  for (std::size_t i = 0; i < song.channels.size(); ++i) {
    auto& channel = song.channels[i];
    channel.name = (i < 16 ? "PCM " : "ADPCM ") + std::to_string(i < 16 ? i + 1 : i - 15);
    channel.orders = {0};
    channel.patterns.push_back({});
  }
  Sample sample;
  sample.name = "Sine PCM8";
  // A periodic, original waveform. Loop makes the explicit Note Off audible.
  for (int i = 0; i < 96; ++i)
    sample.pcm.push_back(static_cast<std::int8_t>(std::lround(96 * std::sin(i * 2 * std::acos(-1.0) / 96))));
  sample.loop = SampleLoop{0, 96};
  song.samples.push_back(sample);
  song.instruments.push_back({"QSound sine", InstrumentType::QSound, 0});
  auto& rows = song.channels[0].patterns[0].rows;
  rows.resize(64);
  rows[0].note = Note{Note::Kind::Pitch, 48};
  rows[0].instrument = 0;
  rows[0].volume = 128;
  rows[32].note = Note{Note::Kind::Off, 0};
  rows[63].effects = {{0xff, 0x00}};
  return song;
}

int main(int argc, char** argv) {
  try {
    check(argc == 2, "Usage: fur_writer_test OUTPUT_DIRECTORY");
    const fs::path output = argv[1];
    fs::create_directories(output);
    const auto song = syntheticSong();
    const auto serialized = FurWriter::serialize(song);
    check(serialized == FurWriter::serialize(song), "serialization is not deterministic");
    int rejected = 0;
    auto invalid = [&](const std::function<void(Song&)>& mutate) {
      auto copy = song;
      mutate(copy);
      try { FurWriter::serialize(copy); }
      catch (const std::invalid_argument&) { ++rejected; return; }
      throw std::runtime_error("invalid model was accepted");
    };
    invalid([](auto& s) { s.systems.clear(); });
    invalid([](auto& s) { s.systems[0].chip = static_cast<Chip>(999); });
    invalid([](auto& s) { s.channels.pop_back(); });
    invalid([](auto& s) { s.channels[0].orders.clear(); });
    invalid([](auto& s) { s.channels[0].orders = {0, 0}; });
    invalid([](auto& s) { s.channels[0].orders[0] = 256; });
    invalid([](auto& s) { s.channels[0].patterns.push_back({}); });
    invalid([](auto& s) { s.channels[0].patterns[0].index = 256; });
    invalid([](auto& s) { s.channels[0].patterns[0].rows.resize(65); });
    invalid([](auto& s) { s.channels[0].effectColumns = 0; });
    invalid([](auto& s) { s.channels[0].effectColumns = 9; });
    invalid([](auto& s) { s.channels[0].patterns[0].rows[0].effects.resize(2); });
    invalid([](auto& s) { s.channels[0].patterns[0].rows[0].note->semitone = 120; });
    invalid([](auto& s) { s.channels[0].patterns[0].rows[0].note->semitone = -61; });
    invalid([](auto& s) { s.channels[0].patterns[0].rows[0].instrument = 1; });
    invalid([](auto& s) { s.instruments[0].sample = 1; });
    invalid([](auto& s) { s.instruments[0].type = static_cast<InstrumentType>(999); });
    invalid([](auto& s) { s.instruments.resize(257); });
    invalid([](auto& s) { s.samples.resize(257); });
    invalid([](auto& s) { s.samples[0].pcm.clear(); });
    invalid([](auto& s) { s.samples[0].pcm.resize(65521); });
    invalid([](auto& s) { s.samples[0].loop->end = 97; });
    invalid([](auto& s) { s.samples[0].loop->start = 96; });
    invalid([](auto& s) { s.samples[0].pcm.resize(32768); s.samples[0].loop = SampleLoop{0, 32768}; });
    invalid([](auto& s) { s.samples[0].rate = 0; });
    invalid([](auto& s) { s.samples[0].c4Rate = 384001; });
    invalid([](auto& s) { s.timing.speed = 0; });
    invalid([](auto& s) { s.timing.rowsPerPattern = 257; });
    invalid([](auto& s) { s.timing.ticksPerSecond = std::numeric_limits<float>::quiet_NaN(); });
    invalid([](auto& s) { s.systems[0].volume = std::numeric_limits<float>::infinity(); });
    invalid([](auto& s) { s.systems[0].pan = -2; });
    invalid([](auto& s) { s.name = std::string("a\0b", 3); });
    invalid([](auto& s) { s.name = "\xc0\x80"; });
    invalid([](auto& s) { s.name = "\xed\xa0\x80"; });
    invalid([](auto& s) { s.instruments[0].name.assign(65535, 'a'); });

    const auto path = output / "synthetic-qsound-v228.fur";
    FurWriter::write(song, path);
    std::ifstream input(path, std::ios::binary);
    std::vector<std::uint8_t> readBack((std::istreambuf_iterator<char>(input)), {});
    check(readBack == serialized, "file differs from in-memory serialization");
    auto bad = song;
    bad.timing.speed = 0;
    try { FurWriter::write(bad, path); throw std::logic_error("invalid write succeeded"); }
    catch (const std::invalid_argument&) {}
    check(fs::file_size(path) == serialized.size(), "validation truncated existing file");
    std::ifstream preserved(path, std::ios::binary);
    check(std::vector<std::uint8_t>((std::istreambuf_iterator<char>(preserved)), {}) == serialized,
          "invalid write changed existing file contents");
    bool ioError = false;
    try { FurWriter::write(song, output); }
    catch (const std::runtime_error&) { ioError = true; }
    check(ioError, "I/O error was not reported");

    // Exercise v228 boundaries and sparse effect masks, independent of playback.
    auto edges = song;
    edges.name = "Granice — próba UTF-8";
    edges.timing.rowsPerPattern = 256;
    edges.samples.resize(256, song.samples[0]);
    edges.instruments.resize(256, song.instruments[0]);
    edges.samples[255].loop.reset();
    edges.samples[255].pcm = {-128, -1, 0, 1, 127};
    edges.instruments[255].sample = 255;
    for (auto& channel : edges.channels) {
      channel.orders.assign(256, 255);
      channel.patterns[0].index = 255;
      channel.patterns[0].rows.clear();
    }
    auto& channel = edges.channels[0];
    channel.effectColumns = 8;
    channel.patterns[0].rows.resize(256);
    auto& first = channel.patterns[0].rows[0];
    first.note = Note{Note::Kind::Pitch, -60};
    first.instrument = 255;
    first.volume = 0;
    first.effects = {{0, 0}, {}, {std::nullopt, 0x12}, {0x13, std::nullopt},
                     {0x14, 0x24}, {}, {std::nullopt, 0}, {0, std::nullopt}};
    channel.patterns[0].rows[255].note = Note{Note::Kind::Pitch, 119};
    FurWriter::write(edges, output / "boundaries-v228.fur");

    auto multiple = song;
    multiple.systems.push_back({});
    multiple.channels.insert(multiple.channels.end(), song.channels.begin(), song.channels.end());
    for (std::size_t i = 0; i < multiple.channels.size(); ++i) {
      auto& ch = multiple.channels[i];
      ch.patterns.push_back(Pattern{1, "Second", {}});
      ch.orders = i % 2 == 0 ? std::vector<std::uint16_t>{0, 1} : std::vector<std::uint16_t>{1, 0};
    }
    FurWriter::write(multiple, output / "two-chips-v228.fur");
    std::cout << "Passed " << rejected << " rejection cases, determinism and file I/O checks.\n"
              << fs::absolute(path) << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
