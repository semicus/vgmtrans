#!/usr/bin/env python3
"""Optional ROM-only integration check for song 20; not registered in CTest/CI.

Independent rational timing and binary decoding; no Furnace implementation code.
"""
import csv
import re
import sys
import zipfile
from fractions import Fraction
from pathlib import Path
from verify_fur import check, decode


def main(rom_path, module_path):
    module = decode(module_path, expected_rates=(24038, 16997))
    check(module['rows'] == 256 and module['orders'] == 1, 'shared 256-row pattern')
    check(module['instruments'] == module['samples'] == 1, 'compact resources')
    trace = list(csv.DictReader(Path(str(module_path) + '.events.tsv').open(), delimiter='\t'))
    events = [{k: int(v) for k, v in e.items()} for e in trace]
    tempos = sorted(set((e['tick'], e['value']) for e in events if e['kind'] == 5))
    check(tempos == [(0, 455), (60, 472), (156, 491), (252, 512)], 'raw tempo points')

    def row(tick):
        driver_periods = Fraction(0)
        for i, (start, tempo) in enumerate(tempos):
            end = min(tick, tempos[i+1][0]) if i+1 < len(tempos) else tick
            if end > start:
                driver_periods += Fraction((end-start)*256, tempo)
        return (driver_periods + Fraction(1, 2)).numerator // (driver_periods + Fraction(1, 2)).denominator

    expected = {}
    ons = offs = 0
    for e in events:
        kind, tick, slot = e['kind'], e['tick'], e['slot']
        if kind in (0, 1):
            expected[slot, row(tick)] = e['value'] - 12 + 60
            ons += 1
            if kind == 1:
                expected[slot, row(tick + e['duration'])] = 180
                offs += 1
        elif kind == 2:
            expected[slot, row(tick)] = 180
            offs += 1
    actual = {}
    for (channel, pattern), cells in module['decoded_patterns'].items():
        for r, cell in enumerate(cells):
            if cell and cell[0] is not None:
                actual[channel, pattern*256+r] = cell[0]
                if cell[0] != 180:
                    check(cell[1:3] == (0, 128), 'instrument/neutral volume')
    check(actual == expected and ons == offs == 7, 'notes and lengths match execution trace')
    check({slot for slot, _ in actual} == {0, 2, 4}, 'original sparse slots')
    check(module['decoded_patterns'][0, 0][203][3][:2] == [255, 0], 'stop row')
    report = Path(str(module_path) + '.txt').read_text()
    record = dict(re.findall(r'(\w+)=(\S+)', next(line for line in report.splitlines() if line.startswith('bank='))))
    check([int(record[k]) for k in ('bank', 'program', 'sampNum', 'root')] == [0,124,106,66], 'resource identity')
    with zipfile.ZipFile(rom_path) as rom:
        names = {Path(name).name.lower(): name for name in rom.namelist()}
        pcm = b''.join(rom.read(names[f'ps-q{i}.{i}k']) for i in range(1,5))
    start, end, signed = module['decoded_samples'][0]
    offset = int(record['pcmOffset'])
    check((start, end) == (-1, -1), 'disabled short tail loop')
    check(bytes(x & 255 for x in signed) == pcm[offset:offset+12884], 'ROM PCM byte identity')
    print('v228 layout, original PCM, sparse slots, 7 notes/7 offs and rational tempo integration verified.')


if __name__ == '__main__':
    main(Path(sys.argv[1]), Path(sys.argv[2]))
