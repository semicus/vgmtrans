# Milestone 2: selected CPS/QSound resources

`CPSFurnaceAdapter::convertResources` accepts a loaded `CPS2InstrSet`, the
associated `VGMSampColl`, and an explicit list of original bank/program pairs.
It returns compact samples, instruments, mappings and diagnostics, without
creating or modifying any musical sequence. Only `CPS2_V1.01` is accepted.
The adapter does not call `convertToMidi`, `prepareForExport` or any GUI API.
The existing scanner loads the ROM and creates the collection in the optional
utility; loading sequences normally for recognition is not sequence export.

## Reproduce locally

Run from the parent `cps-qsound-to-furnace` directory:

```sh
cmake -S vgmtrans -B vgmtrans/build/macos-arm64 \
  -DVGMTRANS_BUILD_FURNACE_TESTS=ON -DVGMTRANS_BUILD_PUNISHER_RESOURCE_TOOL=ON
cmake --build vgmtrans/build/macos-arm64 --config Release -j 6
ctest --test-dir vgmtrans/build/macos-arm64 -C Release --output-on-failure -R furnace
vgmtrans/build/macos-arm64/tests/furnace/Release/punisher_resources \
  test-data/roms/punisher.zip output/punisher-resources-v228.fur
python3 vgmtrans/tests/furnace/verify_punisher_resources.py \
  test-data/roms/punisher.zip output/punisher-resources-v228.fur
```

The output directory must exist. The utility writes a sibling `.fur.txt` diagnostic
report. `--list` instead of the output filename inventories acceptable instruments
and reports rejected entries on stderr. It does not export all 512 entries.

The integration executable is opt-in and is NOT registered with CTest. The ROM,
generated modules and diagnostic files remain outside tracked source files. The
standard `furnace_resources` test uses an entirely synthetic `VirtFile` and requires
no ROM. Both Milestone 1 tests remain enabled and unchanged.

## Selected instruments and audition

Numbers below are decimal. Source banks are 0..1 and programs 0..255. VGMTrans
normalizes them to `bank*2 + program/128`, `program%128`; Furnace indices are compact.

| Furnace instrument/sample | Source bank/program | VGM bank/program | sampNum | Root (MIDI) | Rate | C4Rate | Frames | Loop [start,end) |
|---|---|---|---|---|---|---|---|---|
| 0 / 0 | 0 / 0 | 0 / 0 | 4 | 60 | 24038 | 24038 | 30531 | [12082,30531) |
| 1 / 1 | 0 / 163 | 1 / 35 | 140 | 72 | 24038 | 12019 | 17280 | [11790,17280) |
| 2 / 2 | 1 / 1 | 2 / 1 | 0 | 60 | 24038 | 24038 | 1182 | none |

The utility chooses the first matching collection (`punisher song 0` in the local
ROM). Each instrument gets its own 128-row pattern/order on PCM channel 0:

- row 0: C-4; row 32: hard Note Off;
- row 40: C-5; row 72: hard Note Off;
- row 80: C-3; row 112: hard Note Off;
- last order row 127: FF00.

All notes use volume 128. The other 18 channels are empty. Timing is fixed at
62.5 Hz / speed 1, solely for this manually authored audition. No CPS tempo,
sequence-loop or event interpretation is introduced.

## PCM, loop and tuning contract

- `VGMSamp::toPcm(Signedness::Signed, Endianness::Little, BPS::PCM8)` supplies
  PCM without WAV. Input must already be native mono signed PCM8, unreversed.
  The adapter never resamples it. The local verification script compares every
  exported PCM byte with the concatenated four QSound ROM files and prints hashes.
- Loop units are checked. A mono PCM8 byte is one frame, so both `LM_BYTES` and
  `LM_SAMPLES` are supported. End is `loopStart + loopLength`, calculated in 64 bits
  and required to equal the original sample length. The original table's absolute
  loop address must equal `sample start + loopStart`. End is exclusive; no +/-1.
- Root must agree in the region, sample and sample-info table. The optional
  `ModuleModel::Sample::rootKey` retains the source MIDI-key metadata in memory.
  Furnace has no separate root-key field; `c4Rate` encodes its playback effect.
  Root is also written in sample names and the diagnostic report.
- Conversion uses `round(rate * 2^((60-root)/12))`. Model C-4 is semitone 48,
  corresponding to MIDI 60, and PATN code 108. Fine tuning is neutral: V1.01 has
  no fine-tune field; unexpected nonzero sample/region tuning is rejected.
- Deduplication uses source sample index and root. Instrument identities are
  retained even when they share a sample. No ADSR or macros are emitted.

### Calibration status and manual acceptance

Numerical unit tests cover roots 48, 60 and 72 and unchanged PCM. The local test
checks C-3/C-4/C-5 PATN encodings. These are **not an experimental playback
measurement**, and do not by themselves establish the pitch convention in the
Furnace engine. No Furnace executable was available inside the project during
the final verification; binaries outside the project are excluded by request.

Expected sample traversal rates for the audition are:

| Root | C-3 | C-4 | C-5 |
|---|---|---|---|
| 60 | 12019 | 24038 | 48076 |
| 72 | 6009.5 | 12019 | 24038 |

In Furnace 0.6.8.1, open the generated module, verify the three samples/instruments,
play each order, and check octaves and the two sustained loops. For an objective
engine test, export a VGM from Furnace and inspect PCM channel 0's frequency
register: expected nonzero values for C-4/C-5/C-3 are 4096/8192/2048 for root 60
and 2048/4096/1024 for root 72 (allowing engine rounding). The latter check also
catches an inverted or missing root-key correction, rather than only octave ratios.
Until engine playback/register output has been measured, this acceptance item is
pending. No experimental measurement is claimed by the numerical tests.

## Existing CPS importer heuristics and adapter policy

The underlying importer is not changed:

1. Sample table scan uses sentinel/lookahead logic and subtracts its final 8-byte
   entry. A supplied table length can interact with that subtraction; this adapter
   uses only loaded entries and does not repair or extend the table.
2. End address zero is interpreted as the next bank boundary. End-before-start
   becomes an empty sample; loop addresses outside the sample are clamped to end.
3. The first sample's bank supplies a base-address correction (intended for games
   such as Strider 2). The importer computes its relative loop using the adjusted
   sample offset; nonzero bases can disagree with the original absolute loop.
   The adapter checks that agreement and rejects mismatches rather than repairing them.
4. Samples of length <=2 are padded to 16 for synthesizer compatibility. The adapter
   rejects these using the sample-info table, avoiding export of fabricated bytes.
5. Tail loops shorter than 40 frames are disabled. The adapter preserves the
   imported decision and reports the lost tail loop; a disabled longer loop is
   rejected as inconsistent. In the selected resources, sample 0 loses a 10-frame
   tail loop. The other two loops are retained exactly.
6. The importer can replace invalid sample indices with 0 (and has a >=0x8000
   correction for later variants). V1.01's original one-byte index is compared
   with the resolved region to detect fallback-to-zero. Such instruments are rejected.
7. Samples starting beyond the collection cause the importer to stop; the adapter
   additionally checks the full PCM interval, not just the start offset.
8. CPS2 rate is an integer approximation, 24038 Hz. No alternative rate is inferred
   and no resampling is performed. CPS3's different rate/format is outside scope.
9. The importer converts ADSR tables to common envelope fields. These fields are
   deliberately ignored, so this audition will not reproduce original envelopes.

Other rejection cases include multiple regions, region sample/loop overrides,
missing resources, inconsistent roots, unsupported fine/coarse tuning and writer
limits (PCM <=65520 frames, forward loop <=32767 frames, C4Rate <=384000).
