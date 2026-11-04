# Milestone 3A: executed CPS/QSound sequence values

Historical 3A behavior. For current volume/pan behavior and Song17 reproduction,
see [CONTROLS.md](CONTROLS.md).

Only CPS2_V101 opts into `VGMSeq::captureEvents`. A new read mode runs the existing
parser without creating MIDI or looking up UI events. The sink receives copied
values for each execution, including NoItem calls. Vector order is execution
identity; ROM offsets are diagnostic metadata and may repeat. Notes use the
VGMTrans chromatic-key convention (60 = middle C), with local transpose applied.
The adapter converts to ModuleModel C-4 = 48. The raw trace is track-major; the
planner stably merges by tick, preserving within-slot execution order. At equal ticks, slots are processed in ascending source-slot order, preserving
within-slot changes of state. Conflicting simultaneous tempos fail.

Original pointer-table slots are assigned during CPS2Seq pointer parsing. Empty
slots remain empty and QSound channels 16–18 are unused. Logical banks from the
helpers are decoded as source bank `bank/2`, program `(bank%2)*128+program` for
V1.01. Only instruments selected by actual notes are exported, using the
previously verified resource adapter. Missing program/tempo and unsupported
resource mappings fail explicitly.

## Timing and patterns

For raw tempo T, a sequence tick lasts `256/(T*62.5)` seconds. PPQN is 48;
therefore a quarter lasts `48*256/(T*62.5)`. The clock retains long-double absolute
segment origins; only final absolute times are rounded to 62.5-Hz rows. It never
sums rounded durations. Identical tempo writes in different slots remain in the
trace but define a single segment. No MIDI microseconds-per-quarter rounding is
used. The real driver's fixed-point accumulator phase and tempo-change phase
have **not** been established. The <=8 ms rounding bound is relative to the
integrated timeline, not a proven bound against hardware playback.

Patterns have 256 rows with shared boundaries, no deduplication, and sequential
orders. Duration-generated offs can cross boundaries. A newer monophonic voice
invalidates an older scheduled/keyed off. Such suppressions are reported. Every
cell collision is reported with slot, row and tick; the last event wins. The
selected song has none. Program changes are materialized on the next note;
changes during a sounding note are not applied. Held-note global transposition
changes are not synthesized. Volume is fixed at 128 and pan is neutral.

Capture defaults to 12,000 sequence ticks, zero extra infinite-loop repetitions,
and a one-million-operation guard (also stops zero-time loops). The tick bound
stops before the next parser operation; a rest or pending note duration may end
beyond it. Finite loops execute normally. Forever loops emit a boundary event;
the existing overlapping-loop-ID early termination in CPS is diagnosed as
boundary reason 3. Reasons 1 and 2 mean tick limit and track data boundary.
There are no order jumps. Capture detaches the sink and restores read modes and
conversion options even on exceptions; parser state is reset by the next pass.

## Local reproduction (never CI)

From the vgmtrans directory, with the existing optional resource-tool option ON:

```sh
cmake --build build/macos-arm64 --config Release -j 6
ctest --test-dir build/macos-arm64 -C Release --output-on-failure -R furnace
build/macos-arm64/tests/furnace/Release/punisher_sequence ../test-data/roms/punisher.zip --list
build/macos-arm64/tests/furnace/Release/punisher_sequence ../test-data/roms/punisher.zip ../output/punisher-sequence-v228.fur 20
python3 tests/furnace/verify_punisher_sequence.py ../test-data/roms/punisher.zip ../output/punisher-sequence-v228.fur
```

The output has a `.txt` report and `.events.tsv` raw execution trace. The local
checker independently integrates time using rational arithmetic and verifies
PATN events, pointer/block layout, original PCM bytes, instrument mapping and
empty slots. It is not a playback test. ROM and generated output stay outside
vgmtrans and are not tracked. `punisher_midi ROM.zip OUTPUT_DIRECTORY` exports
all collections solely for bytewise MIDI regression comparison; the sequence
converter never consumes those files.

## Chosen fixture

`punisher song 20`, collection index 20, sequence offset 57871 (0xE20F), natural
end at 384 ticks. Source slots 0, 2, 4; seven ons and seven offs (six explicit,
one duration-generated), three program selections, twelve raw tempo writes:
T=455 at tick 0, 472 at 60, 491 at 156, 512 at 252, each on three slots. Four
segment origins include the initial tempo (three effective changes). No infinite
loop, truncation, collision or synthetic closing note is needed. One pattern per
channel; last source boundary rounds to row 202, stop is at row 203.

All voices use bank 0/program 124/sample 106, root 66, sample rate 24038,
C4Rate 16997, 12884 PCM8 frames. The existing importer disables its 12-frame tail
loop; this is preserved and reported. No resampling. Fine tune is neutral.
Ignored executed opcodes: volume 0x07 (7), pitch bend 0x0c (2), vibrato 0x1b (3),
LFO rate 0x1d (3). ADSR, portamento, panning, echo/DSP, other modulation and 0x19
are outside scope. Even though this fixture uses tied notes, they retain actual
note-on/off timing; portamento between differing pitches is not reconstructed.

CI uses synthetic data only: helper/NoItem events, repeated offsets, sparse
slots, actual CPS finite-loop bytecode and bank opcodes 08/1f, infinite-loop and
operation bounds, tempo segment integration/conflicts, pattern-crossing offs,
collision diagnostics, repeated capture/exception cleanup and MIDI equality
before and after capture. No code was copied or adapted from Furnace; the
existing independent v228 writer and ModuleModel are unchanged.
