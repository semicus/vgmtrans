#!/usr/bin/env python3
"""Optional local integration check. Requires the user's ROM; never registered in CI.

Compares every SMP2 PCM byte with the concatenated QSound ROM region, checks
resource mappings and manually authored notes. Does not claim to test playback.
"""
import hashlib
import re
import struct
import sys
import zipfile
from pathlib import Path

from verify_fur import Reader, check


def main(rom_path, module_path):
    with zipfile.ZipFile(rom_path) as rom:
        names = {Path(name).name.lower(): name for name in rom.namelist()}
        pcm_rom = b"".join(rom.read(names[f"ps-q{i}.{i}k"])
                           for i in range(1, 5))
    data = module_path.read_bytes()
    check(data[:16] == b"-Furnace module-" and struct.unpack_from("<H", data, 16)[0] == 228,
          "expected uncompressed v228")
    blocks = []
    position = 32
    while position < len(data):
        tag = data[position:position + 4]
        size = struct.unpack_from("<I", data, position + 4)[0]
        end = position + 8 + size
        check(end <= len(data), "block outside file")
        blocks.append((tag, position, data[position + 8:end]))
        position = end
    check(position == len(data), "file coverage")
    records = []
    for line in Path(str(module_path) + ".txt").read_text().splitlines():
        if line.startswith("bank="):
            records.append(dict(re.findall(r"(\w+)=(\S+)", line)))
    expected = [(0, 0, 4, 60, 24038, 30531, 12082, 30531),
                (0, 163, 140, 72, 12019, 17280, 11790, 17280),
                (1, 1, 0, 60, 24038, 1182, -1, -1)]
    samples = [payload for tag, _, payload in blocks if tag == b"SMP2"]
    instruments = [payload for tag, _, payload in blocks if tag == b"INS2"]
    check(len(samples) == len(instruments) == len(records) == 3, "compact asset count")
    for index, (payload, record, spec) in enumerate(zip(samples, records, expected)):
        bank, program, sample_num, root, c4rate, length, loop_start, loop_end = spec
        check([int(record[k]) for k in ("bank", "program", "sampNum", "root")] ==
              [bank, program, sample_num, root], "source selection")
        sample = Reader(payload)
        check(sample.text() == f"QSound sample {sample_num} root {root}", "sample name metadata")
        check([sample.number("I") for _ in range(3)] == [length, 24038, c4rate], "sample tuning/length")
        check(sample.take(4) == b"\x08\0\0\0", "mono PCM8 profile")
        check((sample.number("i"), sample.number("i")) == (loop_start, loop_end), "exclusive loop end")
        check([sample.number("I") for _ in range(4)] == [1, 0, 0, 0], "sample presence")
        pcm = sample.take(length)
        sample.done()
        offset = int(record["pcmOffset"])
        check(pcm == pcm_rom[offset:offset + length], "PCM differs from original ROM bytes")
        ins = Reader(instruments[index])
        check((ins.number("H"), ins.number("H")) == (228, 40), "QSound instrument")
        mapping = None
        while True:
            feature = ins.take(2)
            if feature == b"EN":
                break
            feature_data = Reader(ins.take(ins.number("H")))
            if feature == b"SM":
                mapping = feature_data.number("H")
                check(feature_data.take(2) == b"\2\0", "plain sample mapping")
                feature_data.done()
            else:
                check(feature == b"NA", "unexpected instrument macro/feature")
        ins.done()
        check(mapping == index, "instrument sample index")
        print(f"sample {sample_num}: {length} bytes identical to ROM, SHA256 {hashlib.sha256(pcm).hexdigest()}")
    patterns = [(position, payload) for tag, position, payload in blocks if tag == b"PATN"]
    check(len(patterns) == 57, "19 channels, three patterns each")
    locations = set()
    for _, payload in patterns:
        pattern = Reader(payload)
        check(pattern.number("B") == 0, "subsong")
        channel, index = pattern.number("B"), pattern.number("H")
        check((channel, index) not in locations, "duplicate pattern")
        locations.add((channel, index))
        pattern.text()
        if channel != 0:
            check(pattern.take(1) == b"\xff", "unused channel must be empty")
        else:
            for row in range(128):
                mask = pattern.number("B")
                if row in (0, 40, 80):
                    check(mask == 7, "note/instrument/volume presence")
                    check(pattern.take(3) == bytes([{0: 108, 40: 120, 80: 96}[row], index, 128]),
                          "manual C-4/C-5/C-3")
                elif row in (32, 72, 112):
                    check(mask == 1 and pattern.number("B") == 180, "hard Note Off")
                elif index == 2 and row == 127:
                    check(mask == 32 and pattern.take(3) == b"\3\xff\0", "stop effect")
                else:
                    check(mask == 0, "unexpected event")
        pattern.done()
    check(locations == {(ch, index) for ch in range(19) for index in range(3)}, "pattern locations")
    print("Verified original PCM, three mappings, loop bounds and all manual notes. Playback calibration remains separate.")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: verify_punisher_resources.py ROM.zip MODULE.fur")
    main(Path(sys.argv[1]), Path(sys.argv[2]))
