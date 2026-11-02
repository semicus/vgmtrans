# Furnace writer milestone 1

The production code consists of `ModuleModel.h`, `FurWriter.h` and
`FurWriter.cpp`. It has no dependencies on MIDI, Qt, collection objects or
source-format parsers. Validation is private to `FurWriter.cpp`.

## Build and test

From the repository root, using the existing Apple Silicon build directory:

```sh
cmake -S . -B build/macos-arm64 -DVGMTRANS_BUILD_FURNACE_TESTS=ON
cmake --build build/macos-arm64 --config Release -j 6
ctest --test-dir build/macos-arm64 -C Release --output-on-failure -R furnace
```

The optional test target compiles only the writer and the synthetic test
program. Python 3 is required only when this test option is enabled. There are
no new production dependencies. CTest runs both the C++ rejection/I/O tests
and a separately implemented Python binary decoder. Neither uses Furnace code.

The generated file is:

```text
build/macos-arm64/tests/furnace/artifacts/Release/synthetic-qsound-v228.fur
```

To generate it elsewhere, run `build/macos-arm64/tests/furnace/Release/fur_writer_test`
with an output directory argument. The program also writes two boundary fixtures.
Those fixtures exercise binary representation, not musical arrangements.

## Interoperability specification and assumptions

Primary references are `papers/format.md` and `papers/newIns.md` from the local
Furnace tag `v0.6.8.1` (engine format version 228). The tagged reader was consulted
to resolve directory requirements, field ordering and the instrument terminator.
The writer is an independent implementation; no Furnace code is copied, linked,
vendored or adapted into VGMTrans.

- Exactly 16 signature bytes, `-Furnace module-`, with no NUL terminator.
- One subsong stored in `INFO`; no `SONG`, groove, wavetable or macro blocks.
- Little-endian integers, IEEE-754 binary32 floats, NUL-terminated UTF-8 text.
  Embedded NULs and invalid UTF-8 are rejected.
- Block sizes exclude the four-byte ID and four-byte size. Pointers are absolute
  offsets within the uncompressed file. File size and offsets are limited to 32 bits.
- The initial target capability is QSound (`0xe0`), 19 channels per chip. The model
  represents a list of systems, but unsupported chip/instrument types are rejected.
  At most 128 channels are accepted, consistent with the v228 channel limit.
  Adding a target chip later requires a target-format mapping, not a source adapter.
- Time base 0, one-entry speed pattern, virtual tempo 1/1, A-4 tuning 440 Hz.
  Tick rate and speed are model fields. Tick rate is restricted to `(0, 1000]` Hz
  by this profile; speed is 1..255. Highlights 4/16 affect only the editor.
- All basic compatibility flags are zero (including non-linear pitch); the extended
  modern volume-scaling flag is 1, other extended/remaining flags are zero.
  This is an explicit exporter profile, not a claim to duplicate every editor default.
  Effects are represented as raw target-format command/value pairs, without source
  interpretation or emulation of pitch, modulation, envelopes or DSP.
- Chip gain defaults to 1, pan to 0, front/rear balance to 0; master gain is 1.
  The profile accepts chip gain 0..2 and pan -1..1. Explicit left/right patchbay
  connections are emitted for every chip, with automatic patchbay enabled.
- Empty `FLAG` blocks retain Furnace chip defaults. No echo/spatial effects are
  programmed. Furnace's default QSound surround remains enabled; this milestone
  does not change it. All three `ADIR` blocks are emitted with zero directories:
  the v228 loader reads these even for empty asset lists.
- Orders are channel-major; indices are local to each channel. Every order must
  reference a supplied pattern. All channels have the same nonzero order count.
- Limits: 256 rows per pattern, 256 orders, 256 patterns per channel, 256 samples,
  256 instruments and 1..8 effect columns. All supplied patterns are serialized,
  including unused ones. Missing trailing rows are empty; repeated rows are not
  compressed. There is no automatic insertion of notes, effects or song termination.
- Model notes are semitones relative to C-0, not MIDI numbers. C-4 = 48 becomes
  PATN note 108. Supported pitches C-(-5)..B-9 map to bytes 0..179; hard Note Off
  maps to 180. Absence is distinct from explicit zero for all optional cell fields.
- `INS2` uses instrument version 228, target type 40, `NA` and `SM` features,
  and a two-byte `EN` terminator with no length field. `SM` selects one sample,
  sets use-sample, and has no wave or note map. No envelope or macro features.
- Samples are mono signed PCM8 (`SMP2` depth 8). Rate and C-4 rate are independent
  fields; neither resampling nor root-key conversion occurs in this writer.
  The profile accepts rates 1..384000 Hz and nonempty PCM up to 65520 frames
  (the stable QSound renderer's 64 KiB bank limit minus 16 reserved bytes).
  Forward loops use frame indices `[start, end)` and at most 32767 frames.
  No loop is encoded as two signed -1 values. PCM presence is enabled for bank 0
  of each configured chip; other banks are disabled. BRR/dither flags are zero.
  Aggregate hardware sample-memory packing is outside this serialization milestone.
- Serialization validates before opening an output path. Invalid models throw
  `std::invalid_argument`, offset overflow throws `std::length_error`, and file I/O
  failures throw `std::runtime_error`. I/O is checked including close; writing is
  not an atomic file replacement and a mid-write I/O failure can leave a partial file.
- The output is uncompressed. No zlib compression or Furnace runtime dependency.

## Synthetic module

One QSound chip, 19 channels, one 96-frame periodic sine PCM8 sample generated
mathematically (not extracted from a game), sample and C-4 rates 24038 Hz, forward
loop `[0, 96)`, one QSound instrument. The waveform is approximately 250.4 Hz at
its native rate; the test asserts the tracker note C-4, not concert-pitch calibration.

At 62.5 Hz and speed 1, channel 0 plays C-4 at row 0, volume 128, receives hard
Note Off at row 32 (0.512 seconds), then `FF00` at row 63. The other 18 channels
are empty. The entire pattern has 64 rows and there is one order.

## Manual acceptance in Furnace 0.6.8.1

Open the synthetic file and check for no load errors, QSound with 19 channels,
62.5 Hz, speed 1, C-4 on PCM 1, Note Off at row 32, one instrument/sample and
the loop bounds. Play it, then edit a note and save/reopen the module. Confirm a
short audible tone, silence after Note Off and termination at the stop effect.

The automated decoder checks all block boundaries and pointers, all INFO fields,
sample bytes, loops, instrument mappings, timing, note encoding, all effect-mask
positions, UTF-8, index/row/count boundaries and asymmetric channel-major orders.
These tests do not replace opening/playback in the actual Furnace executable.
