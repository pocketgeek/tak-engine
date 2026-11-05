#!/usr/bin/env python3
"""Who gets the credit for a hit and a kill in retail, natively.

Seven pieces, each run on synthetic units:

  * the hitter gate of the damage routine 51a140 -> 51a240: a hitter whose
    object lives (+0x130 & 0x1000000, a dying one too) is stored at +0xf8 and
    its player (+0xfd) at +0xfc and handed to the hit response 409a50; a
    freed object (a retired unit, or the old object of one that changed
    sides) and no hitter leave both as they were and hand 409a50 nothing.
  * the attack multiplier of 52a330: the shooter's +0xe0 only while it lives
    and is not dying (+0x130 & 0x1000).
  * the death weapon 52ac00: a shot with no shooter (+0x7c = 0) for the dying
    unit's player (+0x92 = +0xfd) at its position, through 529c10.
  * retirement 512ae0: the death weapon fires inside it while the unit's
    object still lives, and only for severity > 0 (+0x115) on a finished
    unit (+0x108 == 0); the object is freed after.
  * the mission Kill 513130: every unit this machine runs loses its shots
    (52ae30(u, 1)), then a unit whose Dying runs (+0x130 & 0x10000000)
    retires at once (512ae0) and every other one dies with kind 10 (512610).
  * deadly water 51e0d5..51e14c: once a second (tick % 30 == 0), a unit
    whose height's whole part (+0x6e) is at or below sea level (game
    +0x19ef8), not canhover (UnitDef +0x260 & 0x1000), takes the map's whole
    waterdamage (+0xd45, when +0xd41 is set) through 51a140 with no hitter
    and damage kind 13: no hit response, the last hitter kept.

  * a carried unit in deadly water: 4dad30 commits its carrier's attach
    point as its own +0x68 (51b3b0), and 51e0d5 then reads that height;
    being carried (+0xa8) exempts nothing.

The deadly-water rows are answered by `retail_script_test --deadly-water`
and `--carried-water`.

    python3 tools/re/check_kill_credit.py [build-o2/retail_script_test]
"""
import itertools
import struct
import subprocess
import sys

from emu import Icd, HEAP
from unicorn.x86_const import UC_X86_REG_FPCW, UC_X86_REG_ESI, UC_X86_REG_EBX, UC_X86_REG_EDI, \
    UC_X86_REG_ESP, UC_X86_REG_EBP

STACK_TOP = 0x70000000 + 0x100000 - 0x2000
ALIVE, DYING, DEATH_RUNS = 0x1000000, 0x1000, 0x10000000


class Fixture:
    def __init__(self):
        self.p = Icd()
        (self.game, self.units, self.victim_type, self.local, self.remote, self.shot, self.flags,
         self.map) = (HEAP + i * 0x20000 for i in range(8))
        self.calls = []
        self.put(0x62d55c, self.game)
        self.put(self.local, 1); self.put(self.local + 0xea, 2, 'B')
        self.put(self.remote, 1); self.put(self.remote + 0xea, 3, 'B')
        self.put(self.victim_type + 0x1be, 100)
        self.put(self.game + 0x14e84, self.units); self.put(self.game + 0x14e88, self.units + 7 * 0x138)

    def put(self, address, value, fmt='I'):
        self.p.uc.mem_write(address, struct.pack('<' + fmt, value))

    def get(self, address, fmt='I'):
        return struct.unpack('<' + fmt, self.p.uc.mem_read(address, struct.calcsize(fmt)))[0]

    def record(self, name, nargs, answer=0):
        def hook(uc, sp):
            self.calls.append((name,) + struct.unpack('<%dI' % nargs, uc.mem_read(sp, 4 * nargs)))
            return nargs, answer
        self.p.hooks[int(name, 16)] = hook

    def unit(self, index, owner=None, flags=ALIVE, player=0):
        u = self.units + index * 0x138
        self.p.uc.mem_write(u, bytes(0x138))
        self.put(u + 2, index, 'H'); self.put(u + 0x130, flags); self.put(u + 0xb4, self.victim_type)
        self.put(u + 0xb8, owner or self.local); self.put(u + 0xfd, player, 'B'); self.put(u + 0x10c, 50, 'H')
        return u

    def call(self, address, args=()):
        self.p.uc.reg_write(UC_X86_REG_FPCW, 0x027f)
        result, error = self.p.call(address, args)
        assert not error, error
        return result


def hitter_gate():
    """51a140(hitter, victim, 10, kind 1, 0) against each kind of hitter."""
    f = Fixture()
    f.record('409a50', 3); f.record('56c640', 8)   # the hit response; HitByWeapon's start
    f.put(0x62d558, f.flags); f.put(f.flags + 0x18, f.flags + 0x100)   # no blood
    previous = f.unit(5, player=6)
    rows = []
    for name, flags in (('live', ALIVE), ('dying', ALIVE | DYING | DEATH_RUNS), ('freed', 0), ('none', None)):
        victim = f.unit(1, player=0)
        f.put(victim + 0xf8, previous); f.put(victim + 0xfc, 6, 'B')
        hitter = 0 if flags is None else f.unit(2, flags=flags, player=3)
        f.calls.clear()
        f.call(0x51a140, (hitter, victim, 10, 1, 0))
        stored = f.get(victim + 0xf8), f.get(victim + 0xfc, 'B')
        response = f.calls[0][1] if f.calls else None
        rows.append((name, stored, response, f.get(victim + 0x10c, 'H')))
        if flags is not None and flags & ALIVE:
            assert stored == (hitter, 3) and response == hitter, (name, stored, response)
        else:
            assert stored == (previous, 6) and response == 0, (name, stored, response)
        assert f.get(victim + 0x10c, 'H') == 40, name
    return ('hitter gate: a live or dying hitter is the last hitter (+0xf8, +0xfc) and 409a50\'s; '
            'a freed object or none keeps the old one and hands 409a50 nothing')


def attack_multiplier():
    """52a330(shot, victim, 1.0): the shooter's +0xe0 scales the damage."""
    f = Fixture()
    weapon = f.shot + 0x1000; damage_flags = f.flags
    f.put(0x60596c, 1, 'B'); f.put(0x62d558, damage_flags); f.put(damage_flags, damage_flags + 0x100)
    f.p.hooks[0x531e50] = lambda uc, sp: (1, 100)          # the weapon's damage against the victim
    f.p.hooks[0x5d4444] = lambda uc, sp: (0, 0)            # the CRT draw: no spread
    f.record('51a140', 5)
    answers = {}
    for name, flags in (('live', ALIVE), ('dying', ALIVE | DYING), ('freed', 0), ('none', None)):
        victim = f.unit(1); f.put(victim + 0xe4, 1.0, 'f')
        shooter = 0 if flags is None else f.unit(2, flags=flags)
        if shooter:
            f.put(shooter + 0xe0, 2.0, 'f')
        f.p.uc.mem_write(f.shot, bytes(0xdc)); f.put(f.shot, weapon); f.put(f.shot + 0x7c, shooter)
        f.calls.clear()
        f.call(0x52a330, (f.shot, victim, struct.unpack('<i', struct.pack('<f', 1.0))[0]))
        answers[name] = f.calls[0][3]
    base = answers['none']   # the CRT draw (hooked to 0) is the spread's low end
    assert answers == {'live': 2 * base, 'dying': base, 'freed': base, 'none': base}, answers
    return 'attack multiplier: +0xe0 scales a live shooter\'s damage only; dying, freed or none: x1'


def death_weapon_shot():
    """52ac00(unit, selfdestruct): the shot it hands 529c10."""
    f = Fixture()
    shots = []
    def impact(uc, sp):
        shot = struct.unpack('<I', uc.mem_read(sp, 4))[0]
        shots.append(bytes(uc.mem_read(shot, 0xdc)))
        return 2, 0
    f.p.hooks[0x529c10] = impact
    u = f.unit(1, player=4)
    f.put(f.victim_type + 0x1b6, 0x1234); f.put(f.victim_type + 0x1ba, 0x5678)
    for i, value in enumerate((0x110000, 0x220000, 0x330000)):
        f.put(u + 0x68 + 4 * i, value)
    rows = []
    for selfdestruct in (0, 1):
        shots.clear()
        f.call(0x52ac00, (u, selfdestruct))
        shot = shots[0]
        weapon, x, y, z = struct.unpack_from('<4I', shot, 0)
        shooter = struct.unpack_from('<I', shot, 0x7c)[0]; player = shot[0x92]
        assert weapon == (0x5678 if selfdestruct else 0x1234) and (x, y, z) == (0x110000, 0x220000, 0x330000)
        assert shooter == 0 and player == 4, (shooter, player)
    return 'death weapon: 52ac00\'s shot has no shooter (+0x7c 0), the dying unit\'s player, its position'


def retirement():
    """512ae0 with its callees stubbed: when the death weapon fires."""
    f = Fixture()
    stubs = {'4c6750': 2, '4c6800': 2, '523510': 1, '50aa20': 1, '51b4f0': 5, '5066a0': 1,
             '512ee0': 5, '5ba5d0': 0, '4ee560': 1, '4dc7f0': 0, '4eba00': 0, '4c7190': 0}
    for name, nargs in stubs.items():
        f.record(name, nargs)
    scenario = f.flags; vtable = f.flags + 0x100; stub = f.flags + 0x200
    f.put(0x62da3c, scenario); f.put(scenario, vtable); f.put(vtable + 0x18, stub)
    f.p.uc.mem_write(stub, b'\xc2\x04\x00')                    # ret 4
    f.put(f.game + 0x175dc, f.map)
    alive_at_blast = []
    def blast(uc, sp):
        unit = struct.unpack('<I', uc.mem_read(sp, 4))[0]
        alive_at_blast.append(f.get(unit + 0x130) & ALIVE)
        f.calls.append(('52ac00', unit))
        return 2, 0
    f.p.hooks[0x52ac00] = blast
    cases = []
    ordered = 0
    for severity, finished, suppressed, kind, corpse in itertools.product((0, 1, 60), (0, 1), (0, 1), (1, 3, 5, 7),
                                                                          (0, 1, 2)):
        u = f.unit(1, flags=ALIVE | DYING | DEATH_RUNS)
        f.put(u + 0x115, severity, 'B'); f.put(u + 0x116, kind << 4 | corpse, 'B')
        f.calls.clear()
        f.put(u + 0x108, 0.0 if finished else 0.5, 'f'); f.put(u + 0xfc, 10, 'B')
        f.put(f.game + 0x19f94, 2 if suppressed else 0)
        alive_at_blast.clear()
        f.call(0x512ae0, (u,))
        fired = bool(alive_at_blast)
        assert fired == (severity > 0 and finished and not suppressed), (severity, finished, suppressed, kind)
        assert all(alive_at_blast), 'the object was freed before its death weapon'
        assert not f.get(u + 0x130) & ALIVE, 'retirement left the object alive'
        # The corpse (512ee0, for a corpse kind in +0x116's low nibble) comes
        # after the death weapon: the blast never reaches its own corpse.
        names = [c[0] for c in f.calls]
        assert ('512ee0' in names) == (corpse > 0), (corpse, names)
        if fired and corpse:
            assert names.index('52ac00') < names.index('512ee0'), names
            ordered += 1
        cases.append(fired)
    return ('retirement: 512ae0 fires the death weapon (%d of %d cases) while the object lives, for '
            'severity > 0 on a finished unit, then frees it; the corpse (512ee0) after the blast in all %d '
            'cases with both' % (sum(cases), len(cases), ordered))


def mission_kill():
    """513130 over a local live, a local dying, a local retiring, a freed and a remote unit."""
    f = Fixture()
    f.record('52ae30', 2); f.record('512610', 2); f.record('512ae0', 1)
    live = f.unit(1); dying = f.unit(2, flags=ALIVE | DYING); retiring = f.unit(3, flags=ALIVE | DYING | DEATH_RUNS)
    f.unit(4, flags=0); f.unit(5, owner=f.remote)
    for i in (0, 6):
        f.p.uc.mem_write(f.units + i * 0x138, bytes(0x138))
    f.call(0x513130)
    expected = [('52ae30', live, 1), ('512610', live, 10), ('52ae30', dying, 1), ('512610', dying, 10),
                ('52ae30', retiring, 1), ('512ae0', retiring)]
    assert f.calls == expected, f.calls
    return ('mission Kill: every local unit loses its shots; a retiring one retires at once, '
            'every other one dies with kind 10; freed and remote units are skipped')


def deadly_water(binary):
    """51e0d5..51e152 for every combination of its inputs, against the port."""
    f = Fixture()
    f.record('51a140', 5)
    from unicorn import UC_HOOK_CODE
    f.p.uc.hook_add(UC_HOOK_CODE, lambda uc, *_: uc.emu_stop(), begin=0x51e357, end=0x51e357)   # no local owner
    unit_type = f.victim_type
    rows, expected = [], []
    for frame, height, sea, hover, enabled, amount, owner in itertools.product(
            (0, 1, 29, 30, 31, 60, 899, 900), (-3, 0, 4, 5, 6, 200), (0, 5, 255), (0, 1), (0, 1),
            (0, 1, 100, 30000), (1, 2, 3)):
        u = f.unit(1, owner={1: f.local, 2: f.local, 3: f.remote}[owner])
        f.put(f.local + 0xea, owner if owner != 3 else 1, 'B')
        y = ((height << 16) | 0x8000) & 0xffffffff
        f.put(u + 0x6c, y)
        f.put(unit_type + 0x260, 0x1000 if hover else 0)
        f.put(f.game + 0x19f44, frame); f.put(f.game + 0x19ef8, sea, 'B'); f.put(f.game + 0x175dc, f.map)
        f.put(f.map + 0xd41, enabled); f.put(f.map + 0xd45, amount)
        f.calls.clear()
        f.p.uc.reg_write(UC_X86_REG_ESI, u); f.p.uc.reg_write(UC_X86_REG_EBX, f.game)
        f.p.uc.reg_write(UC_X86_REG_EDI, 0)
        f.p.uc.reg_write(UC_X86_REG_ESP, STACK_TOP); f.p.uc.reg_write(UC_X86_REG_EBP, STACK_TOP + 0x100)
        f.p.uc.reg_write(UC_X86_REG_FPCW, 0x027f)
        f.p.uc.emu_start(0x51e0d5, 0x51e152, count=200)
        if f.calls:
            assert f.calls[0][1] == 0 and f.calls[0][2] == u and f.calls[0][4] == 13, f.calls
        if owner == 3:
            continue   # a remote player's unit: its own machine hurts it (lockstep runs every unit)
        rows.append('%d %d %d %d %d %d' % (frame, struct.unpack('<i', struct.pack('<I', y))[0], sea, hover,
                                          enabled, amount))
        expected.append('%d' % (f.calls[0][3] if f.calls else 0))
    output = subprocess.run([binary, '--deadly-water'], input='\n'.join(rows) + '\n',
                            text=True, capture_output=True, check=True)
    actual = output.stdout.splitlines()
    assert len(actual) == len(expected), (len(actual), len(expected))
    for row, got, want in zip(rows, actual, expected):
        if got != want:
            raise AssertionError('%s\n  port   %s\n  native %s' % (row, got, want))
    hurt = sum(1 for e in expected if e != '0')
    return 'deadly water: %d rows match (%d hurt), no hitter, kind 13' % (len(rows), hurt)


def carried_water(binary):
    """Deadly water on a carried unit: 4dad30 commits the carrier's attach
    point as the cargo's +0x68 (51b3b0), then 51e0d5 reads that +0x6e; being
    carried (+0xa8) exempts nothing. Natively chained, against the port."""
    import random
    from unicorn import UC_HOOK_CODE
    f = Fixture()
    f.record('51a140', 5)
    committed = []

    def commit(uc, sp):
        unit, x, y, z, _flags = struct.unpack('<Iiiii', uc.mem_read(sp, 20))
        committed.append((unit, x, y, z))
        return 5, 0
    f.p.hooks[0x51b3b0] = commit
    stop = [0]
    f.p.uc.hook_add(UC_HOOK_CODE, lambda uc, *_: (stop.__setitem__(0, 1), uc.emu_stop()),
                    begin=0x4dadd7, end=0x4dadd7)
    host, mover, kind = HEAP + 0x300000, HEAP + 0x301000, HEAP + 0x302000
    rng = random.Random(0x51e13f)
    rows, expected = [], []
    for case in range(600):
        sea = rng.choice((0, 20, 50, 128, 255))
        hy = rng.choice((sea - 1, sea, sea + 1, sea - 20, sea + 60, rng.randrange(-40, 300)))
        y = ((hy << 16) | rng.randrange(0x10000)) & 0xffffffff
        floater, waterline = rng.randrange(2), rng.choice((0, 3, 12, 40))
        amount = rng.choice((1, 100, 30000))
        tick = rng.choice((0, 30, 31, 900))
        cargo = f.unit(1)
        p = f.p
        p.uc.mem_write(host, bytes(0x200)); p.uc.mem_write(mover, bytes(0x100)); p.uc.mem_write(kind, bytes(0x300))
        f.put(host + 0x68, 4096 << 16); f.put(host + 0x6c, y); f.put(host + 0x70, 2048 << 16)
        f.put(cargo + 0xa8, host); f.put(cargo + 0xb4, kind); f.put(cargo + 0x112, 0xff, 'B')
        f.put(kind + 0x260, 0x80000 if floater else 0); f.put(kind + 0x248, waterline, 'B')
        f.put(f.game + 0x19ef8, sea, 'B'); f.put(f.game + 0x19f44, tick); f.put(f.game + 0x175dc, f.map)
        f.put(f.map + 0xd41, 1); f.put(f.map + 0xd45, amount)
        committed.clear(); stop[0] = 0
        p.uc.reg_write(UC_X86_REG_FPCW, 0x027f)
        _, error = p.call(0x4dad30, (cargo,), ecx=mover, allow_early_stop=True)
        assert not error and stop[0] and len(committed) == 1 and committed[0][0] == cargo, (error, committed)
        _, cx, cy, cz = committed[0]
        f.put(cargo + 0x68, cx & 0xffffffff); f.put(cargo + 0x6c, cy & 0xffffffff); f.put(cargo + 0x70, cz & 0xffffffff)
        f.calls.clear()
        p.uc.reg_write(UC_X86_REG_ESI, cargo); p.uc.reg_write(UC_X86_REG_EBX, f.game)
        p.uc.reg_write(UC_X86_REG_EDI, 0)
        p.uc.reg_write(UC_X86_REG_ESP, STACK_TOP); p.uc.reg_write(UC_X86_REG_EBP, STACK_TOP + 0x100)
        p.uc.emu_start(0x51e0d5, 0x51e152, count=200)
        assert f.get(cargo + 0xa8) == host
        rows.append('%d %d %d %d %d %d' % (struct.unpack('<i', struct.pack('<I', y))[0], floater, waterline,
                                           sea, amount, tick))
        expected.append('%d' % (f.calls[0][3] if f.calls else 0))
    output = subprocess.run([binary, '--carried-water'], input='\n'.join(rows) + '\n',
                            text=True, capture_output=True, check=True)
    actual = output.stdout.splitlines()
    assert len(actual) == len(expected), (len(actual), len(expected))
    for row, got, want in zip(rows, actual, expected):
        if got != want:
            raise AssertionError('%s\n  port   %s\n  native %s' % (row, got, want))
    hurt = sum(1 for e in expected if e != '0')
    return 'carried in deadly water: %d rows match (%d hurt at the carrier\'s attach point)' % (len(rows), hurt)


def water_hit():
    """The kind-13 hit itself: whole HP, no hit response, the last hitter kept."""
    f = Fixture()
    f.record('409a50', 3)
    v = f.unit(1); hitter = f.unit(2, player=5)
    f.put(v + 0xf8, hitter); f.put(v + 0xfc, 5, 'B')
    f.call(0x51a140, (0, v, 30, 13, 0))
    assert f.get(v + 0x10c, 'H') == 20 and not f.calls and f.get(v + 0xf8) == hitter
    f.call(0x51a140, (0, v, 30, 13, 0))
    assert f.get(v + 0x10c, 'h') == -10 and f.get(v + 0x130) & DYING and f.get(v + 0x10f, 'B') == 13
    return 'water hit: whole HP off, no 409a50, last hitter kept, kind 13 on the lethal one'


def water_death():
    """51e13f..51e371: a lethal water hit's 512610 runs at the end of the same
    update, after the missions (4d8450, skipped for a dying unit), the mover
    (4dc800) and the surface (51b2a0); a hit that leaves HP above zero runs
    none. Natively: 51a140 itself, the rest of the update's calls recorded."""
    from unicorn import UC_HOOK_CODE
    f = Fixture()
    for name, n in (('429d90', 4), ('401160', 1), ('4d8450', 1), ('4d85e0', 1), ('4dc800', 1),
                    ('51b2a0', 1), ('5195e0', 1), ('512060', 9), ('512610', 2), ('409a50', 3)):
        f.record(name, n)
    f.p.uc.hook_add(UC_HOOK_CODE, lambda uc, *_: uc.emu_stop(), begin=0x51e371, end=0x51e371)
    seen = []
    for hp, amount in ((50, 100), (50, 50), (50, 49), (300, 100)):
        u = f.unit(1); f.put(u + 0x10c, hp, 'H'); f.put(u + 8, f.units + 0x1000)   # a mover
        f.put(f.victim_type + 0x260, 0); f.put(f.victim_type + 0x268, 0); f.put(f.victim_type + 0x21e, 0.0, 'f')
        f.put(u + 0x6c, (2 << 16) | 0x8000)
        f.put(f.game + 0x19f44, 30); f.put(f.game + 0x19ef8, 5, 'B'); f.put(f.game + 0x175dc, f.map)
        f.put(f.map + 0xd41, 1); f.put(f.map + 0xd45, amount)
        f.calls.clear()
        f.p.uc.reg_write(UC_X86_REG_ESI, u); f.p.uc.reg_write(UC_X86_REG_EBX, f.game)
        f.p.uc.reg_write(UC_X86_REG_EDI, 0)
        f.p.uc.reg_write(UC_X86_REG_ESP, STACK_TOP); f.p.uc.reg_write(UC_X86_REG_EBP, STACK_TOP + 0x100)
        f.p.uc.reg_write(UC_X86_REG_FPCW, 0x027f)
        f.p.uc.emu_start(0x51e0d5, 0x51e371, count=2000)
        names = [c[0] for c in f.calls]
        lethal = hp <= amount
        assert bool(f.get(u + 0x130) & DYING) == lethal, (hp, amount)
        if lethal:
            assert names[-1] == '512610' and f.calls[-1][1:] == (u, 13), f.calls
            assert names.index('4dc800') < names.index('51b2a0') < names.index('512610'), names
            assert '4d8450' in names, names          # called; it returns at once for a dying unit (4d8475)
        else:
            assert '512610' not in names, names
        seen.append(lethal)
    return ('water death: a lethal water hit\'s 512610 runs at the end of the same update (kind 13); '
            '%d lethal, %d not' % (seen.count(True), seen.count(False)))


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else 'build-o2/retail_script_test'
    for line in (hitter_gate(), attack_multiplier(), death_weapon_shot(), retirement(), mission_kill(),
                 water_hit(), water_death(), deadly_water(binary), carried_water(binary)):
        print(line)


if __name__ == '__main__':
    main()
