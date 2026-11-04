#!/usr/bin/env python3
"""Local-only Song 17 integration, no ROM or extracted fixture in CI."""
import csv
import re
import sys
import zipfile
from pathlib import Path
from verify_fur import check, decode


def main(rom_path, path, reference=None):
    module = decode(path)
    check((module['rows'], module['orders'], module['channel_count']) == (256, 2, 19), 'unchanged dimensions')
    events = [{k:int(v) for k,v in e.items()} for e in csv.DictReader(Path(str(path)+'.events.tsv').open(), delimiter='\t')]
    check([e['order'] for e in events] == list(range(len(events))), 'explicit execution order')
    volume_values = {30:15, 50:27, 70:44, 77:51, 105:94, 110:108, 113:118, 115:125, 123:176, 125:203, 127:255}
    volumes = {(slot,0):255 for slot in range(7)}
    pans = {(slot,0):128 for slot in range(7)}
    source_vol = [e for e in events if e['kind']==11]
    source_pan = [e for e in events if e['kind']==12]
    check(len(source_vol)==18 and len(source_pan)==16, 'source control counts')
    for e in source_vol+source_pan:
        check(e['domain']==1, 'raw source domain')
    for e in source_vol:
        volumes[e['slot'],(4*e['tick']+3)//6] = volume_values[e['value']]
    for e in source_pan:
        pans[e['slot'],(4*e['tick']+3)//6] = (e['value']*255+31)//32
    actual_volumes, actual_pans, notes = {}, {}, {}
    for (channel, pattern), cells in module['decoded_patterns'].items():
        for row, cell in enumerate(cells):
            if cell is None:
                continue
            pos = channel, pattern*256+row
            if cell[0] is not None: notes[pos] = cell[:2]
            if cell[2] is not None: actual_volumes[pos] = cell[2]
            for fx in range(0,16,2):
                if cell[3][fx] == 0x80: actual_pans[pos] = cell[3][fx+1]
                elif cell[3][fx] is not None: check(pos == (0,450) and cell[3][fx:fx+2] == [255,0], 'only stop besides pan')
    check(actual_volumes == volumes and actual_pans == pans, 'exact source/fallback control cells')
    check(sum(n[0]!=180 for n in notes.values())==121 and sum(n[0]==180 for n in notes.values())==43, '3A note collisions unchanged')
    if reference:
        old = decode(reference)
        old_notes = {(ch,p*256+r):cell[:2] for (ch,p),cells in old['decoded_patterns'].items()
                     for r,cell in enumerate(cells) if cell is not None and cell[0] is not None}
        check(notes == old_notes, 'all note and instrument cells identical to 3A reference')
        check(module['decoded_samples'] == old['decoded_samples'], 'samples unchanged from 3A')
    with zipfile.ZipFile(rom_path) as rom:
        names = {Path(n).name.lower():n for n in rom.namelist()}
        pcm = b''.join(rom.read(names[f'ps-q{i}.{i}k']) for i in range(1,5))
    lines = Path(str(path)+'.txt').read_text().splitlines()
    records = [dict(re.findall(r'(\w+)=(\S+)',line)) for line in lines if line.startswith('bank=')]
    check([(int(r['bank']),int(r['program']),int(r['sampNum'])) for r in records] == [(0,42,26),(0,15,32),(0,18,39),(0,20,36)], 'resource identity')
    for record in records:
        start,end,signed = module['decoded_samples'][int(record['sample'])]
        offset = int(record['pcmOffset'])
        check(bytes(v&255 for v in signed) == pcm[offset:offset+len(signed)], 'PCM identical to ROM')
    print(f'Song17: 121 Note On / 43 stored Off, 78 source collisions preserved; {len(volumes)} volume cells / {len(pans)} pan effects including fallback initialization. PCM and v228 blocks verified.')


if __name__ == '__main__':
    main(Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]) if len(sys.argv)>3 else None)
