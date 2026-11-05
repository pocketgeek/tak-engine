#!/usr/bin/env python3
"""Check how retail fills a new unit's mana pool, natively, against the port.

The pool is a {current, maximum} pair of floats at unit +0xd8 / +0xdc, and
four small routines are all that write it: 401000 (current 0, maximum given),
4010d0 (spend if affordable), 401100 (add, capped at the maximum) and 401120
(set, capped at the maximum). This script

  * scans .text for every call to them and to the creation chain (S): 401000
    is called once, from 511990, which only 511f60 calls, which only 512060
    and 512460 call -- every new unit (production, a skirmish start, a mission
    placement, Create, a revive, a change of owner) starts with an empty pool;
    401120 only from the placement record (514cf2) and SetAttribute (4d3ed2);
    401100 only from regeneration (40119e) and the ManaMe cheat (4263c8);
  * runs 401000 (E): the current value becomes 0, the maximum the argument;
  * runs the record reader's percentages (4c8dc3..4c8df0, the getter 543190
    native with the CRT atoi, only its key lookup 543110 answered) (E):
    HealthPercentage defaults to 100 and ManaPercentage to 0, read as atoi
    into words;
  * runs the placement's HP and mana (514c9d..514cf7) and SetAttribute's
    (4d3e6a..4d3e8a, 4d3eb9..4d3ed7) with 401120 native (E), for a grid of
    MaxMana, maxdamage and percentages, and requires the same pool bits and
    hit points from `new_unit_mana_test --native` (MissionScript::percentMana /
    percentHp);
  * runs the regeneration step 401160 (E): ManaRechargeRate is added only when
    the construction left (+0x108) is 0, and the sum is capped at MaxMana.

    python3 tools/re/check_new_unit_mana.py [build-o2/new_unit_mana_test]
"""
import struct
import subprocess
import sys

import capstone
from emu import Icd, HEAP, ICD, STACK, STACK_SZ
from unicorn.x86_const import (UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_EDI, UC_X86_REG_ESI,
                               UC_X86_REG_ESP, UC_X86_REG_FPCW)

TEXT = (0x401000, 0x1e9912, 0x1000)


def calls(targets):
    """Every direct call in .text to one of `targets`, as {target: [site, ...]}."""
    data = open(ICD, 'rb').read()
    vma, size, off = TEXT
    text = data[off:off + size]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    found = {t: [] for t in targets}
    at = 0
    while at < len(text):
        moved = False
        for i in md.disasm(text[at:at + 0x4000], vma + at):
            moved = True
            if i.mnemonic == 'call' and i.op_str.startswith('0x'):
                t = int(i.op_str, 16)
                if t in found:
                    found[t].append(i.address)
            at = i.address + i.size - vma
        if not moved:
            at += 1
    return found


def static_writers():
    got = calls((0x401000, 0x4010d0, 0x401100, 0x401120, 0x511990, 0x511f60))
    want = {0x401000: [0x511c9d], 0x401120: [0x4d3ed2, 0x514cf2], 0x401100: [0x40119e, 0x4263c8],
            0x4010d0: [0x401201, 0x42811d, 0x5302d6], 0x511990: [0x511fae], 0x511f60: [0x512274, 0x51253f]}
    for t, sites in want.items():
        assert sorted(got[t]) == sites, (hex(t), [hex(s) for s in got[t]])
    return ('the pool is written only by 401000 (511990), 401120 (514cf2 placement, 4d3ed2 SetAttribute), '
            '401100 (40119e regeneration, 4263c8 ManaMe) and 4010d0 (spending); 511990 runs under 511f60 '
            'from 512060 and 512460 only')


def init_pool(p):
    pool = HEAP + 0x100
    p.uc.mem_write(pool, struct.pack('<ff', 77.0, 5.0))
    _, error = p.call(0x401000, (struct.unpack('<I', struct.pack('<f', 123.5))[0],), ecx=pool)
    assert not error, error
    cur, top = struct.unpack('<ff', p.uc.mem_read(pool, 8))
    assert (cur, top) == (0.0, 123.5), (cur, top)
    return '401000: a new pool holds 0 of its MaxMana'


def record_defaults():
    p = Icd()
    record, text = HEAP + 0x1000, HEAP + 0x2000
    answers = {}

    def lookup(uc, sp):   # 543110(key) -> the value string, or 0 when the key is missing
        key = bytes(uc.mem_read(struct.unpack('<I', uc.mem_read(sp, 4))[0], 32)).split(b'\0')[0].decode()
        value = answers.get(key)
        if value is None:
            return 1, 0
        uc.mem_write(text, value.encode() + b'\0')
        return 1, text
    p.hooks[0x543110] = lookup
    p.freeze_hooks()
    rows = []
    for given in ({}, {'HealthPercentage': '25', 'ManaPercentage': '40'},
                  {'HealthPercentage': '50.9', 'ManaPercentage': '150'}, {'ManaPercentage': 'x'}):
        answers.clear(); answers.update(given)
        p.uc.mem_write(record, bytes(0x40))
        p.uc.reg_write(UC_X86_REG_ESI, record); p.uc.reg_write(UC_X86_REG_EBX, HEAP + 0x3000)
        p.uc.reg_write(UC_X86_REG_ESP, STACK + STACK_SZ - 0x2000)
        p.uc.reg_write(UC_X86_REG_FPCW, 0x027f)
        p.uc.emu_start(0x4c8dc3, 0x4c8df0)
        rows.append(struct.unpack('<hh', p.uc.mem_read(record + 0x1a, 4)))
    assert rows == [(100, 0), (25, 40), (50, 150), (100, 0)], rows
    return 'the record reads HealthPercentage (default 100) and ManaPercentage (default 0) with atoi into words'


def percentages(p):
    """(maxmana bits, maxdamage, pct) -> (placement mana bits, hp), (SetAttribute mana bits, hp)."""
    unit, tp, record, frame = HEAP + 0x4000, HEAP + 0x5000, HEAP + 0x6000, STACK + STACK_SZ - 0x3000

    def run(start, end, maxmana, maxhp, pct, setattr):
        p.uc.mem_write(unit, bytes(0x200)); p.uc.mem_write(tp, bytes(0x300)); p.uc.mem_write(record, bytes(0x40))
        p.uc.mem_write(unit + 0xb4, struct.pack('<I', tp))
        p.uc.mem_write(unit + 0xd8, struct.pack('<fI', 9.0, maxmana))
        p.uc.mem_write(tp + 0x1be, struct.pack('<I', maxhp))
        p.uc.mem_write(record + 0x1a, struct.pack('<hh', 100, pct))
        p.uc.mem_write(frame + 0x10, struct.pack('<i', pct))
        p.uc.reg_write(UC_X86_REG_ESI, unit)
        p.uc.reg_write(UC_X86_REG_EDI, pct if setattr else record)
        p.uc.reg_write(UC_X86_REG_EBP, frame); p.uc.reg_write(UC_X86_REG_ESP, frame - 0x100)
        p.uc.reg_write(UC_X86_REG_FPCW, 0x027f)
        p.uc.emu_start(start, end)

    def placed(maxmana, maxhp, pct):
        run(0x514c9d, 0x514cf7, maxmana, maxhp, pct, False)
        mana = struct.unpack('<I', p.uc.mem_read(unit + 0xd8, 4))[0]
        # the placement's HP takes the record's HealthPercentage: run it again with it as pct
        p.uc.mem_write(record + 0x1a, struct.pack('<h', pct))
        p.uc.reg_write(UC_X86_REG_EBP, frame); p.uc.reg_write(UC_X86_REG_ESP, frame - 0x100)
        p.uc.reg_write(UC_X86_REG_ESI, unit); p.uc.reg_write(UC_X86_REG_EDI, record)
        p.uc.emu_start(0x514c9d, 0x514cd2)
        return mana, struct.unpack('<H', p.uc.mem_read(unit + 0x10c, 2))[0]

    def scripted(maxmana, maxhp, pct):
        run(0x4d3eb9, 0x4d3ed7, maxmana, maxhp, pct, True)
        mana = struct.unpack('<I', p.uc.mem_read(unit + 0xd8, 4))[0]
        run(0x4d3e6a, 0x4d3e8a, maxmana, maxhp, pct, True)
        return mana, struct.unpack('<H', p.uc.mem_read(unit + 0x10c, 2))[0]
    return placed, scripted


def regeneration(p):
    unit, tp = HEAP + 0x8000, HEAP + 0x9000
    p.hooks  # 51e4d0 (the cloak bit) is hooked by the caller
    out = []
    for left, cur in ((0.0, 0.0), (0.5, 0.0), (0.0, 999.9), (0.0, 1000.0)):
        p.uc.mem_write(unit, bytes(0x200)); p.uc.mem_write(tp, bytes(0x300))
        p.uc.mem_write(unit + 0xb4, struct.pack('<I', tp))
        p.uc.mem_write(unit + 0x130, struct.pack('<I', 0x1000000))
        p.uc.mem_write(unit + 0x108, struct.pack('<f', left))
        p.uc.mem_write(unit + 0xd8, struct.pack('<ff', cur, 1000.0))
        p.uc.mem_write(tp + 0x1a6, struct.pack('<f', 1.0 / 3.0))
        p.uc.reg_write(UC_X86_REG_FPCW, 0x027f)
        _, error = p.call(0x401160, (unit,))
        assert not error, error
        out.append(struct.unpack('<f', p.uc.mem_read(unit + 0xd8, 4))[0])
    third = struct.unpack('<f', struct.pack('<f', 1.0 / 3.0))[0]
    assert out == [third, 0.0, 1000.0, 1000.0], out
    return '401160: ManaRechargeRate is added only with no construction left (+0x108 == 0), capped at MaxMana'


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else 'build-o2/new_unit_mana_test'
    print(static_writers())
    p = Icd()
    p.hooks[0x51e4d0] = lambda uc, sp: (2, 0)
    p.freeze_hooks()
    print(init_pool(p))
    print(record_defaults())
    print(regeneration(p))
    placed, scripted = percentages(p)
    rows, expected = [], []
    for maxmana in (0.0, 20.0, 150.0, 1000.0, 5000.0, 333.3, 0.7):
        bits = struct.unpack('<I', struct.pack('<f', maxmana))[0]
        for maxhp in (0, 1, 100, 651, 15750, 65535):
            for pct in (0, 1, 7, 10, 25, 33, 45, 99, 100, 101, 150, 1000):
                a, b = placed(bits, maxhp, pct), scripted(bits, maxhp, pct)
                assert a == b, (maxmana, maxhp, pct, a, b)
                rows.append('%d %d %d' % (bits, maxhp, pct))
                expected.append('%d %d' % a)
    out = subprocess.run([binary, '--native'], input='\n'.join(rows) + '\n', text=True,
                         capture_output=True, check=True).stdout.splitlines()
    assert len(out) == len(expected), (len(out), len(expected))
    for row, got, want in zip(rows, out, expected):
        if got.strip() != want:
            raise AssertionError('%s\n  port   %s\n  native %s' % (row, got, want))
    print('PASS: %d HealthPercentage / ManaPercentage cases match retail 514c9d and SetAttribute 4d3e6a/4d3eb9 '
          '(hit points chopped to a word, mana capped at MaxMana)' % len(rows))


if __name__ == '__main__':
    main()
