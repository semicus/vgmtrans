#!/usr/bin/env python3
"""Read back actual v228 cells using the existing independent binary decoder."""
from pathlib import Path
import sys
from verify_fur import decode, check
m = decode(Path(sys.argv[1]) / 'controls-v228.fur')
rows = m['decoded_patterns'][2, 0]
check(rows[0][:3] == (None, None, 0) and rows[0][3][:2] == [0x80, 0], 'zero volume/pan without note')
check(rows[1][:3] == (108, 0, 108) and rows[1][3][:2] == [0x80, 128], 'note + intermediate volume + center')
check(rows[2][:3] == (None, None, 255) and rows[2][3][:2] == [0x80, 64], 'merged control-only row')
check(rows[3][:3] == (112, 0, 255) and rows[3][3][:2] == [0x80, 255], 'note + maximum volume + right')
check(rows[4][:3] == (115, 1, None) and rows[4][3] == [None]*16, 'instrument change retains level')
check(rows[5][0] == 180, 'note off retained')
check(m['decoded_patterns'][2,1][0][3][:2] == [0x80,0], 'second pattern pan')
check(m['decoded_patterns'][0,1][3][3][:4] == [255,0,0x80,128], 'stop and pan in separate effect columns')
print('v228 volume and 80xx cells round-trip passed.')
