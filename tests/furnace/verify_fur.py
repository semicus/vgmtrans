#!/usr/bin/env python3
"""Independent, read-only checks of generated v228 fixtures (Python stdlib only).

This is a test decoder of the documented subset, not Furnace implementation code.
Every referenced block must be consumed exactly, and every file byte accounted for.
"""
import math
import struct
import sys
from pathlib import Path


def check(condition, message):
    if not condition:
        raise ValueError(message)


class Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def take(self, count):
        check(0 <= count <= len(self.data) - self.pos, "truncated field")
        result = self.data[self.pos:self.pos + count]
        self.pos += count
        return result

    def number(self, fmt):
        return struct.unpack("<" + fmt, self.take(struct.calcsize("<" + fmt)))[0]

    def text(self):
        end = self.data.index(0, self.pos)
        return self.take(end - self.pos + 1)[:-1].decode("utf-8")

    def done(self):
        check(self.pos == len(self.data), "unexpected trailing bytes in block")


def decode(path):
    data = path.read_bytes()
    header = Reader(data[:32])
    check(header.take(16) == b"-Furnace module-", "uncompressed signature")
    check(header.number("H") == 228, "format version")
    check(header.number("H") == 0, "reserved header")
    info_pointer = header.number("I")
    check(header.take(8) == bytes(8), "reserved header tail")
    spans = [(0, 32)]

    def block(pointer, expected):
        check(32 <= pointer <= len(data) - 8, "invalid block pointer")
        check(data[pointer:pointer + 4] == expected.encode("ascii"), "block ID: " + expected)
        size = struct.unpack_from("<I", data, pointer + 4)[0]
        end = pointer + 8 + size
        check(end <= len(data), "block outside file")
        spans.append((pointer, end))
        return Reader(data[pointer + 8:end])

    info = block(info_pointer, "INFO")
    check(info.take(4) == bytes([0, 1, 1, 1]), "legacy timing")
    check(info.number("f") == 62.5, "tick rate")
    rows, orders = info.number("H"), info.number("H")
    check(info.take(2) == bytes([4, 16]), "highlights")
    instruments, waves, samples = [info.number("H") for _ in range(3)]
    patterns = info.number("I")
    chips = info.take(32)
    chip_count = chips.index(0)
    check(chips == bytes([0xe0] * chip_count) + bytes(32 - chip_count), "chip list")
    channel_count = chip_count * 19
    check(info.take(64) == bytes(64), "legacy output fields")
    flag_pointers = [info.number("I") for _ in range(32)]
    name, author = info.text(), info.text()
    check(info.number("f") == 440, "tuning")
    check(info.take(20) == bytes(20), "basic compatibility flags")
    instrument_pointers = [info.number("I") for _ in range(instruments)]
    check(waves == 0, "no wavetables in profile")
    sample_pointers = [info.number("I") for _ in range(samples)]
    pattern_pointers = [info.number("I") for _ in range(patterns)]
    order_table = [list(info.take(orders)) for _ in range(channel_count)]
    columns = list(info.take(channel_count))
    check(info.take(channel_count) == bytes([3] * channel_count), "visible channels")
    check(info.take(channel_count) == bytes(channel_count), "channel collapse")
    names = [info.text() for _ in range(channel_count)]
    short_names = [info.text() for _ in range(channel_count)]
    comment = info.text()
    check(info.number("f") == 1, "master gain")
    check(info.take(28) == bytes(16) + b"\1" + bytes(11), "extended flags")
    check((info.number("H"), info.number("H")) == (1, 1), "virtual tempo")
    check((info.text(), info.text()) == ("", ""), "subsong metadata")
    check(info.take(4) == bytes(4), "no additional subsongs")
    check([info.text() for _ in range(6)] == [""] * 6, "extended metadata")
    for _ in range(chip_count):
        check([info.number("f") for _ in range(3)] == [1, 0, 0], "chip output")
    connections = [info.number("I") for _ in range(info.number("I"))]
    expected_connections = [((chip * 16 + port) << 16) | port
                            for chip in range(chip_count) for port in range(2)]
    check(connections == expected_connections, "stereo patchbay")
    check(info.number("B") == 1, "automatic patchbay")
    check(info.take(8) == bytes(8), "remaining compat flags")
    check(info.take(17) == b"\1\1" + bytes(15), "speed pattern")
    check(info.number("B") == 0, "no grooves")
    directories = [info.number("I") for _ in range(3)]
    info.done()

    for pointer in directories:
        directory = block(pointer, "ADIR")
        check(directory.number("I") == 0, "empty asset directory")
        directory.done()
    for pointer in flag_pointers[:chip_count]:
        flags = block(pointer, "FLAG")
        check(flags.text() == "", "default chip flags")
        flags.done()
    check(flag_pointers[chip_count:] == [0] * (32 - chip_count), "unused chip pointers")

    instrument_samples = []
    for pointer in instrument_pointers:
        ins = block(pointer, "INS2")
        check((ins.number("H"), ins.number("H")) == (228, 40), "QSound instrument header")
        features = {}
        while True:
            code = ins.take(2)
            if code == b"EN":
                break
            check(code not in features, "duplicate instrument feature")
            features[code] = Reader(ins.take(ins.number("H")))
        ins.done()
        check(set(features) == {b"NA", b"SM"}, "minimal instrument features")
        features[b"NA"].text()
        features[b"NA"].done()
        sm = features[b"SM"]
        sample = sm.number("H")
        check(sample < samples, "instrument sample mapping")
        check(sm.take(2) == b"\2\0", "use sample without sample map")
        sm.done()
        instrument_samples.append(sample)

    decoded_samples = []
    for pointer in sample_pointers:
        sample = block(pointer, "SMP2")
        sample.text()
        length = sample.number("I")
        check((sample.number("I"), sample.number("I")) == (24038, 24038), "sample rates")
        check(sample.take(4) == b"\10\0\0\0", "signed PCM8 format")
        start, end = sample.number("i"), sample.number("i")
        check((start, end) == (-1, -1) or 0 <= start < end <= length, "loop bounds")
        check([sample.number("I") for _ in range(4)] == [(1 << chip_count) - 1, 0, 0, 0],
              "sample presence")
        pcm = list(struct.unpack(f"{length}b", sample.take(length)))
        sample.done()
        decoded_samples.append((start, end, pcm))

    decoded_patterns = {}
    for pointer in pattern_pointers:
        pattern = block(pointer, "PATN")
        check(pattern.number("B") == 0, "first subsong")
        channel, index = pattern.number("B"), pattern.number("H")
        check(channel < channel_count and index < 256, "pattern location")
        check((channel, index) not in decoded_patterns, "unique pattern location")
        pattern.text()
        cells = []
        while len(cells) < rows:
            mask = pattern.number("B")
            if mask == 255:
                break
            if mask & 128:
                cells.extend([None] * ((mask & 127) + 2))
                continue
            effects = (pattern.number("B") if mask & 32 else 0)
            effects |= (pattern.number("B") << 8 if mask & 64 else 0)
            effects |= (1 if mask & 8 else 0) | (2 if mask & 16 else 0)
            note = pattern.number("B") if mask & 1 else None
            instrument = pattern.number("B") if mask & 2 else None
            volume = pattern.number("B") if mask & 4 else None
            fx = [pattern.number("B") if effects & (1 << bit) else None for bit in range(16)]
            check(not any(x is not None for x in fx[columns[channel] * 2:]), "effect-column bounds")
            check(instrument is None or instrument < instruments, "instrument index")
            check(note is None or note <= 182, "v228 note encoding")
            cells.append((note, instrument, volume, fx) if mask else None)
        check(len(cells) <= rows, "pattern row overflow")
        cells.extend([None] * (rows - len(cells)))
        pattern.done()
        decoded_patterns[channel, index] = cells
    for channel, channel_orders in enumerate(order_table):
        check(all((channel, index) in decoded_patterns for index in channel_orders), "order resolution")
    spans.sort()
    check(spans[0][0] == 0 and spans[-1][1] == len(data), "file coverage")
    check(all(left[1] == right[0] for left, right in zip(spans, spans[1:])), "overlapping blocks or gaps")
    return locals()


def main(directory):
    basic = decode(directory / "synthetic-qsound-v228.fur")
    check((basic["chip_count"], basic["channel_count"], basic["rows"], basic["orders"]) == (1, 19, 64, 1),
          "synthetic song dimensions")
    check((basic["instruments"], basic["samples"], basic["patterns"]) == (1, 1, 19), "asset counts")
    cells = basic["decoded_patterns"][0, 0]
    check(cells[0][:3] == (108, 0, 128), "C-4 with instrument 0 and volume 128")
    check(cells[32][0] == 180 and cells[63][3][:2] == [255, 0], "note off and stop")
    check(all(cell is None for row, cell in enumerate(cells) if row not in (0, 32, 63)), "empty rows")
    check(all(all(cell is None for cell in pattern) for (channel, _), pattern in
              basic["decoded_patterns"].items() if channel != 0), "unused channels")
    start, end, pcm = basic["decoded_samples"][0]
    expected_pcm = [int(math.copysign(math.floor(abs(96 * math.sin(i * 2 * math.pi / 96)) + 0.5),
                                    math.sin(i * 2 * math.pi / 96))) for i in range(96)]
    check((start, end) == (0, 96) and pcm == expected_pcm, "looped sine PCM bytes")

    edge = decode(directory / "boundaries-v228.fur")
    check((edge["rows"], edge["orders"], edge["instruments"], edge["samples"]) == (256, 256, 256, 256),
          "v228 limits")
    first = edge["decoded_patterns"][0, 255][0]
    check(first[:3] == (0, 255, 0), "lowest note, instrument 255, explicit zero volume")
    check(first[3] == [0, 0, None, None, None, 0x12, 0x13, None,
                       0x14, 0x24, None, None, None, 0, 0, None], "all effect masks")
    check(edge["decoded_patterns"][0, 255][255][0] == 179, "highest note at last row")
    check(edge["decoded_samples"][255] == (-1, -1, [-128, -1, 0, 1, 127]), "unlooped signed PCM")
    check(edge["instrument_samples"][255] == 255, "sample 255")
    check(all(set(order) == {255} for order in edge["order_table"]), "order index 255")
    multi = decode(directory / "two-chips-v228.fur")
    check(multi["channel_count"] == 38 and multi["chip_count"] == 2, "multiple target chips")
    check(multi["order_table"] == [[0, 1] if i % 2 == 0 else [1, 0] for i in range(38)],
          "channel-major order table")
    print("Verified complete v228 block layout, references, timing, notes, PCM, loops and effect masks.")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
