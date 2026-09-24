#!/usr/bin/env python3
"""Compare spawn/stance animation inputs with the local retail setter and GET 46."""
import random
import struct
import subprocess
import sys
from emu import Icd, HEAP

p = Icd()
unit, script, host = [HEAP + i * 0x10000 for i in range(3)]
def put(address, value):
    p.uc.mem_write(address, struct.pack('<I', value & 0xffffffff))
def call(address, args, ecx=0):
    result, error = p.call(address, args, ecx=ecx)
    if error:
        raise RuntimeError(error)
    return result
put(script + 0xa64, host)
put(host + 0xc, unit)
rng = random.Random(0x5198a0)
rows, expected = [], []
for i in range(2048):
    enabled = i & 1
    standing, move, fire = (rng.randrange(4) for _ in range(3))
    request = (i // 2) % 4  # 3 retains the initial/default standing order
    flags = (rng.getrandbits(32) & ~0x13f0000) | (enabled << 24)
    flags |= (standing << 20) | (fire << 18) | (move << 16)
    put(unit + 0x130, flags)
    if request < 3:
        call(0x5198a0, (unit, request))
    expected.append(tuple(call(0x50ceb0, (query, 0, 0, 0, 0), script)
                          for query in (46, 2, 3)))
    rows.append(f'{enabled} {standing} {move} {fire} {request}')
result = subprocess.run([sys.argv[1] if len(sys.argv) > 1 else
                         'build-o2/animation_roster_test', '--standing-order'],
                        input='\n'.join(rows) + '\n', text=True,
                        capture_output=True, check=True)
actual = [tuple(map(int, line.split())) for line in result.stdout.splitlines()]
assert actual == expected, next(((rows[i], a, b) for i, (a, b) in
                                enumerate(zip(actual, expected)) if a != b),
                               'length mismatch')
print('PASS: 2048 standing-order/default/setter cases match retail GET 46, 2 and 3')
