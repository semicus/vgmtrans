# Milestone 3B — raw CPS/QSound volume and panning

Scope: CPS2_V1.01, The Punisher song 17 at 0xDAB3. Existing capture, resource
adapter, ModuleModel and independent v228 writer remain the pipeline. There is no
MIDI intermediate. No Furnace code or tables were copied or adapted; the only
shared amplitude table is existing zlib-licensed VGMTrans data, moved unchanged
from CPS2TrackV1.cpp to CPSVolumeTable.h.

## Volume analysis and mapping

Opcode 07 reads an unsigned byte. Its valid domain is 0..127, indexing the
existing 128-entry CPS amplitude table A. This is not a linear volume index.
A[0]=0, A[127]=0x1FFD (8189), and the parser normalizes by 0x1FFF (8191).
VGMTrans stores that normalized level at 14-bit resolution. Its MIDI path uses
an amplitude-to-MIDI conversion (square root), which is **not** used here.

The v0.6.8.1 QSound dispatch reports volume maximum 255, not 128. With a QSound
instrument (INS2 type 40), no volume macro, and native QSound mode, its register
level is floor(volume*16383/255). The chip applies that register multiplicatively
to PCM. Therefore the exporter preserves VGMTrans's normalized channel gain:

    FurnaceVolume = round(255 * A[CPSRaw] / 8191)
                  = (255 * A[CPSRaw] + 4095) / 8191  [integer division]

Examples: 0->0, 30->15, 50->27, 70->44, 77->51, 110->108, 125->203, 127->255.
There is no ad-hoc boost or MIDI CC-derived gain. The curve is nonlinear in the
source index and linear in the decoded amplitude. Eight-bit volume loses some
resolution (including tiny amplitudes rounding to zero).

This deliberately maps **normalized channel gain**, not an asserted raw QSound
register dump. The source table's 8191 normalization is taken from the existing
parser; Furnace's native register full scale is 16383. Absolute register-level
agreement with the game has not been established. ADSR/sustain/release, source
master volume (1A), mixer gain and DSP remain outside this milestone, so an
absolute-loudness match is not claimed. Changing this normalization later needs
actual driver/register evidence, rather than an arbitrary listening gain.

For V1.01, instrument records contain sample and envelope fields, with no decoded
per-instrument volume adjustment. Channel volume is state and is not reset on a
program change. The exporter preserves this state rather than writing a constant
volume on every note. Future envelope support will be a separate contribution.

## Pan analysis and mapping

Opcode 18 reads positions 0..32: 0 left, 16 center, 32 right. The old parser
converts this to 0 or 4*p-1 before its MIDI pan conversion. Those transformed
values are retained solely for the unchanged MIDI/UI path. The sink receives p.

Furnace v0.6.8.1 processes effect 80xx through linear-position-to-split gains and
then QSound split-to-position conversion. Together their integer quantization
maps effect byte x to floor(32*x/255). To preserve all 33 source positions:

    Effect80Value = ceil(255 * CPSRaw / 32)
                  = (255 * CPSRaw + 31) / 32  [integer division]

Examples: 0->8000, 2->8010, 8->8040, 16->8080, 17->8088, 24->80C0, 32->80FF.
Ordinary nearest rounding is wrong for some positions above center: e.g. source
17 rounded to x=135 would select target position 16. All 33 indices are tested.

This is an **index-preserving approximation of source pan**, not a claim of
linear L/R audio gains or hardware-exact stereo. QSound has reversed left/right
mix tables, separate wet/dry tables and filters/delays. Even the alternative
non-spatial table is not simply two linear gain ramps. Furnace enables its
QSound algorithm by default. This exporter leaves that setting unchanged: it
adds no 12xx surround toggle and no echo/spatial DSP implementation. Original
Punisher driver table selection and the effect of ignored opcode 19 have not
been verified. Full hardware stereo behavior remains unproven.

Reference-only inspection used local Furnace tag v0.6.8.1:
- src/engine/platform/qsound.cpp: GET_VOLMAX, VOLUME, NOTE_ON, PANNING
- src/engine/platform/qsound.h: default channel pan and surround state
- src/engine/playback.cpp: effect 80
- src/engine/engine.cpp: pan position/split conversion behavior
- src/engine/platform/sound/qsound.c: register scale and wet/dry pan behavior
- doc/7-systems/qsound.md: effect 12 / default algorithm

## Execution, order, state and quantization

Volume/Panning kinds are appended to the existing enum (11/12). Each event has
slot, absolute sequence tick, ROM offset, raw value, value-domain tag, and a
capture-wide ordinal assigned by VGMSeq. Raw values pass as optional metadata
through addVol/addPan to addLevelNoItem/addPanNoItem; direct addVolNoItem is also
covered. Native helper values are tagged separately; the CPS adapter rejects
those instead of silently treating them as raw opcodes. The old MIDI calculations
and control state are unchanged. Source-invalid values fail explicitly.

The timeline stores every execution. Controls are processed chronologically,
with source-slot order for equal ticks and stable execution order within a slot.
They use the same absolute-time clock and row quantization as notes. Source
changes write volume-only or effect-only rows as necessary. Notes never reset
volume, and pan never replaces volume or notes. Effects occupy separate columns;
the writer supports simultaneous effects without changes to production code.

Multiple same-kind controls in one row retain the final source state and report
intermediate-state loss. A row cannot express source order between a note and a
later sub-row control; the report explicitly flags shared Note On rows. Volume
and pan of different kinds coexist. No attempt is made to fix the 78 Note Off /
Note On collisions, alter Hz, or simulate driver phase.

Musical channels receive deterministic fallback volume 255 and pan 8080 at row
zero, overwritten by explicit controls. These are assumptions for missing
initial state, not invented source events. In Song17 every musical channel has
explicit volume and pan before its first note, so fallback values do not decide
its audible initial level. The report includes each channel's first-note state.

## Song17 result

121 Note On / 121 pre-quantization Off, 78 unchanged collisions, 674 sequence
ticks / 7.18933333333 seconds, end row 449 and Stop at 450. Two orders and 256-row
patterns, 62.5 Hz, speed 1, v228. Instruments/samples and all note/instrument
cells are unchanged from 3A.

- Volume: 18 source events -> 18 cells, source 30..127 -> target 15..255;
  no same-kind merges or rejected events. 24 cells including initialization.
- Pan: 16 source events -> 16 effects, source 2..30 -> target 16..240;
  no same-kind merges or rejected events. 22 effects including initialization.
- Thirteen control writes share a Note On row (8 volume, 5 pan). These source
  controls precede the corresponding note; no source state inversion is needed.
- Six additional initialization cells of each type remain before later source
  writes; slot 6 initialization is replaced by explicit tick-zero controls.

First-note state, all explicit:

| Slot | Volume raw | Volume Furnace | Pan raw | Effect |
|---:|---:|---:|---:|---|
| 0 | 110 | 108 | 30 | 80F0 |
| 1 | 125 | 203 | 25 | 80C8 |
| 2 | 110 | 108 | 20 | 80A0 |
| 3 | 110 | 108 | 15 | 8078 |
| 4 | 110 | 108 | 10 | 8050 |
| 5 | 77 | 51 | 5 | 8028 |
| 6 | 127 | 255 | 2 | 8010 |

## Verification and local reproduction

CI tests use synthetic source data only. ControlsTest covers endpoints,
intermediate values, all pan positions, controls before/during notes, instrument
state persistence, simultaneous notes/volume/pan, multiple same-row changes,
pattern boundaries and diagnostic counters. SequenceTest exercises actual CPS
07/18 bytecode and NoItem helpers, execution ordinals and MIDI replay. The Python
readback checks actual volume and 80xx bytes, zero values, and multiple effect
columns through the independent v228 decoder.

From vgmtrans, with the existing optional local tools enabled:

```sh
cmake --build build/macos-arm64 --config Release -j 6
ctest --test-dir build/macos-arm64 -C Release --output-on-failure -R furnace
build/macos-arm64/tests/furnace/Release/punisher_sequence ../test-data/roms/punisher.zip ../output/punisher-song17-v228.fur 17
python3 tests/furnace/verify_punisher_song17.py ../test-data/roms/punisher.zip ../output/punisher-song17-v228.fur ../output/punisher-song17-3a-reference.fur
build/macos-arm64/tests/furnace/Release/punisher_midi ../test-data/roms/punisher.zip ../output/midi-after-3b
```

The optional final argument compares all note/instrument cells and PCM against
the preserved local 3A file. Without it, the integration check still checks raw
control writes, resource identity, ROM PCM, v228 structure and note counts.
ROM-dependent tools are not CTest/CI tests. Historical verify_punisher_sequence.py
is a checker for the **neutral-volume 3A Song20 fixture**, not current 3B output.
No ROM, extracted assets or generated modules are added to the repository.
