#!/usr/bin/env python3
"""Differential check of Retail group pacing against the original routines.

Loads randomized group records (through retail's own setters) and units into
the emulated KINGDOMS.icd and compares, case by case, with the engine's
helpers in src/sim/retailgroup.h and retailGroundFormation:

  tick       51b890 group tick incl. straggler pass and the changed-flag
             notification (game+0x3070 bit 0, navigator vt+0x30/vt+0x38)
  query      51c700 centre, 51ce40 radius, 51d1e0 out-of-slot
  check      402b00 head: Move_Ground's two group checks (+50c480 on cancel)
  formation  402880 Move_Ground_Formation, all four stages

Observation only: nothing from the binary is copied into the engine.

    python3 -B tools/re/check_retail_group.py [--cases N] [--build DIR]
"""
import argparse, os, random, struct, subprocess, sys
from emu import Icd, HEAP
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_FPCW

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SETTERS = {  # class -> (count all, count moving, area all, area moving, centre all, centre moving)
    0: (0x50b840, 0x50b8c0, 0x50b940, 0x50b9c0, 0x50ba40, 0x50bb00),
    1: (0x50bc60, 0x50bce0, 0x50bd60, 0x50bde0, 0x50be60, 0x50bf20),
    2: (0x50c080, 0x50c100, 0x50c180, 0x50c200, 0x50c280, 0x50c340),
}
GAME, PLAYER, TABLE, UNITS, TYPES, MOVERS, NAVS, MISSIONS, VT = (
    HEAP, HEAP + 0x2404, HEAP + 0x20000, HEAP + 0x30000, HEAP + 0x40000,
    HEAP + 0x50000, HEAP + 0x60000, HEAP + 0x80000, HEAP + 0x90000)
HOOK30, HOOK38 = HEAP + 0x91000, HEAP + 0x91010
STRIDE = 0x138


def s16(v): return ((v & 0xffff) ^ 0x8000) - 0x8000


class Harness:
    def __init__(self):
        self.p = Icd(); self.uc = self.p.uc; self.uc.reg_write(UC_X86_REG_FPCW, 0x027f)
        self.events = []
        def nav30(uc, sp):
            nav = uc.reg_read(UC_X86_REG_ECX); arg = struct.unpack('<i', uc.mem_read(sp, 4))[0]
            self.events.append(('fb', (nav - NAVS) // 0x40, arg)); return 1, 0
        def nav38(uc, sp):
            self.events.append(('notify', (uc.reg_read(UC_X86_REG_ECX) - NAVS) // 0x40)); return 0, 0
        self.p.hooks[HOOK30] = nav30; self.p.hooks[HOOK38] = nav38
        self.draws = []
        def rand(uc, sp):
            n = struct.unpack('<i', uc.mem_read(sp, 4))[0]; self.events.append(('rand', n))
            return 1, self.draws.pop(0) if self.draws else 0
        self.p.hooks[0x535cc0] = rand
        def record(name, nargs, ret=0):
            def f(uc, sp):
                args = struct.unpack('<%di' % nargs, uc.mem_read(sp, 4 * nargs)) if nargs else ()
                self.events.append((name, uc.reg_read(UC_X86_REG_ECX)) + tuple(args)); return nargs, ret
            return f
        self.p.hooks[0x4d6a50] = record('cancel', 2)
        self.p.hooks[0x4d7750] = record('push', 2)
        self.p.hooks[0x4d4d40] = record('stop', 1)
        self.p.hooks[0x4d6a10] = record('sleep', 1)
        def goal(uc, sp):
            vec, radius = struct.unpack('<Ii', uc.mem_read(sp, 8))
            x, y, z = struct.unpack('<iii', uc.mem_read(vec, 12))
            self.events.append(('move', s16(x >> 16), s16(z >> 16), radius)); return 2, 0
        self.p.hooks[0x4d4da0] = goal
        self.p.hooks[0x4d8370] = record('scan', 1, 0)
        self.alloc = HEAP + 0xa0000
        def alloc(uc, sp): return 0, self.alloc
        self.p.hooks[0x4eb9e0] = alloc
        def name(uc, sp): return 1, uc.reg_read(UC_X86_REG_ECX)
        self.p.hooks[0x4d4bf0] = name
        def ctor(uc, sp):
            args = struct.unpack('<12i', uc.mem_read(sp, 48))
            self.events.append(('formation', args[4], args[5]))
            this = uc.reg_read(UC_X86_REG_ECX); uc.mem_write(this + 4, b'\1'); return 12, this
        self.p.hooks[0x4d6c40] = ctor
        self.uc.hook_add(1 << 2, self._stop, begin=0x402d53, end=0x402d53)  # UC_HOOK_CODE
        self.p.freeze_hooks()
        self.put('I', 0x62d55c, GAME); self.put('I', PLAYER, 1); self.put('I', PLAYER + 0x84, TABLE)
        self.put('I', VT + 0x30, HOOK30); self.put('I', VT + 0x38, HOOK38)

    def _stop(self, uc, address, size, _):
        self.events.append(('moveground',)); uc.emu_stop()

    def put(self, fmt, address, *values): self.uc.mem_write(address, struct.pack('<' + fmt, *values))
    def get(self, fmt, address): return struct.unpack('<' + fmt, self.uc.mem_read(address, struct.calcsize(fmt)))

    def call(self, address, args, ecx=None):
        _, error = self.p.call(address, args, ecx=ecx)
        if error and 'UC_ERR_OK' not in error: raise AssertionError(error)

    def load_record(self, group, rec):
        base = TABLE + group * 0xc4
        self.uc.mem_write(base, b'\0' * 0xc4)
        self.put('I', base + 8, rec['members'])
        for cls in range(3):
            names = SETTERS[cls]
            for part, (cnt, area, centre) in enumerate((rec['all'][cls], rec['moving'][cls])):
                self.call(names[0 + part], (PLAYER, group, cnt))
                self.call(names[2 + part], (PLAYER, group, area))
                self.call(names[4 + part], (PLAYER, group) + tuple((c << 16) - (1 << 32) * ((c << 16) >= 1 << 31) for c in centre))
        self.call(0x50b7a0, (PLAYER, group, rec['active']))
        self.call(0x50bbc0, (PLAYER, group, rec['gs'])); self.call(0x50bfe0, (PLAYER, group, rec['bs']))
        self.call(0x50c400, (PLAYER, group, rec['changed']))

    def read_record(self, group):
        base = TABLE + group * 0xc4
        out = [self.get('i', base + 0x2c)[0], self.get('i', base + 0xb0)[0], self.get('i', base + 0x58)[0],
               self.get('i', base + 0x84)[0]]
        for off in (0x30, 0x5c, 0x88):
            cnt_a, cnt_m, area_a, area_m = self.get('4i', base + off)
            ca = self.get('3i', base + off + 0x10); cm = self.get('3i', base + off + 0x1c)
            out += [cnt_a, area_a] + [s16(v >> 16) for v in ca] + [cnt_m, area_m] + [s16(v >> 16) for v in cm]
        return out

    def load_unit(self, i, u):
        a = UNITS + i * STRIDE; t = TYPES + i * 0x300; m = MOVERS + i * 0x40; n = NAVS + i * 0x40
        ms = MISSIONS + i * 0x80
        self.uc.mem_write(a, b'\0' * STRIDE); self.uc.mem_write(t, b'\0' * 0x300); self.uc.mem_write(ms, b'\0' * 0x80)
        self.put('I', a + 8, m if u['mover'] else 0); self.put('I', m, n); self.put('I', n, VT)
        self.uc.mem_write(m + 0x36, struct.pack('<H', u['terrain']))
        self.put('I', a + 0x60, ms if u['mission'] else 0); self.put('I', ms + 0x5a, u['flags'])
        for k, v in zip((0x68, 0x6c, 0x70), u['fixed']): self.put('i', a + k, v)
        self.uc.mem_write(a + 0x78, struct.pack('<hh', u['fx'], u['fz']))
        self.put('f', a + 0x108, 0.0 if u['complete'] else 0.5)
        self.put('I', a + 0x130, u['ustate']); self.put('I', a + 0xb4, t); self.put('I', a + 0xb8, PLAYER)
        self.put('I', a + 0xc8, u['group'])
        self.put('i', t + 0x162, u['type_max']); self.put('i', t + 0x16e, u['water']); self.put('i', t + 0x172, u['road'])
        self.uc.mem_write(t + 0x194, struct.pack('<h', u['depth'])); self.put('I', t + 0x260, u['tflags'])
        return a


def scaled(u):
    if u['terrain'] & 0x800: return (u['road'] * u['type_max']) >> 16
    if u['terrain'] & 0x1000: return (u['water'] * u['type_max']) >> 16
    return u['type_max']


def kind(u):
    if u['tflags'] & 0x800: return 2
    if u['mover'] and u['tflags'] & 0x80000 and u['depth'] > 0: return 1
    return 0


def member_text(u):
    return '%d %d %d %d %d %d' % (kind(u), int(u['mover']), s16(u['fixed'][0] >> 16), s16(u['fixed'][1] >> 16),
                                  s16(u['fixed'][2] >> 16), u['fx'] * u['fz'])


def record_text(r):
    parts = [r['members'] != 0, r['active'], r['changed'], r['gs'], r['bs']]
    for cls in range(3):
        for cnt, area, c in (r['all'][cls], r['moving'][cls]): parts += [cnt, area] + list(c)
    return ' '.join(str(int(v)) for v in parts)


def random_record(rng, members=True, spread=600):
    def part():
        n = rng.choice([0, 1, 2, 3, 5, 9]); return (n, rng.randrange(0, 40) if n else rng.choice([0, 3]),
                                                 tuple(rng.randrange(0, spread) for _ in range(3)))
    return dict(members=int(members), active=rng.random() < 0.85, changed=rng.random() < 0.3,
                gs=rng.choice([0, rng.randrange(1, 300000)]), bs=rng.choice([0, rng.randrange(1, 300000)]),
                all=[part() for _ in range(3)], moving=[part() for _ in range(3)])


def random_unit(rng, group, spread=600):
    tflags = rng.choice([0, 0, 0, 0x80000, 0x800])
    flags = rng.choice([0, 0x3000400, 0x3000400, 0x11000000, 0x5012000, 0x15000400, 0x1000000, 0x2000000])
    return dict(group=group, mover=rng.random() < 0.95, mission=flags != 0 or rng.random() < 0.2, flags=flags,
                terrain=rng.choice([0, 0, 0x800, 0x1000]), tflags=tflags, depth=rng.choice([0, 4]),
                fixed=tuple((rng.randrange(0, spread) << 16) | rng.randrange(65536) for _ in range(3)),
                fx=rng.randrange(1, 5), fz=rng.randrange(1, 5), complete=rng.random() < 0.95,
                ustate=0x1000000 | (0x1000 if rng.random() < 0.05 else 0) if rng.random() < 0.95 else 0,
                type_max=rng.randrange(20000, 200000), road=rng.choice([65536, 78643]),
                water=rng.choice([32768, 65536]))


def eligible(u):
    return bool(u['ustate'] & 0x1000000) and not u['ustate'] & 0x1000 and u['complete'] and u['mover']


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--cases', type=int, default=400)
    ap.add_argument('--build', default=os.path.join(ROOT, 'build-dbg', 'tmp'))
    a = ap.parse_args()
    driver = os.path.join(a.build, 'retail_group_driver')
    subprocess.check_call(['g++', '-std=c++20', '-O1', '-I', os.path.join(ROOT, 'src'), '-o', driver,
                           os.path.join(ROOT, 'tools', 're', 'retail_group_driver.cpp')])
    ours = subprocess.Popen([driver], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    def ask(text, lines=1):
        ours.stdin.write(text + '\n'); ours.stdin.flush()
        return [ours.stdout.readline().strip() for _ in range(lines)]
    h = Harness(); rng = random.Random(0x51b890); counts = dict(tick=0, query=0, check=0, formation=0)
    stats = dict(notified=0, slowed=0, reform=0, wait=0, cancel=0)
    # 51b890 tick
    for case in range(a.cases):
        h.events.clear()
        notify = rng.random() < 0.5; kindp = rng.choice([1, 2, 3])
        h.put('I', GAME + 0x3070, int(notify)); h.uc.mem_write(PLAYER + 0xea, bytes([kindp, 0]))
        groups = rng.sample(range(1, 100), 4)
        recs = {g: random_record(rng, members=rng.random() < 0.8) for g in range(1, 100)}
        for g in range(1, 100):
            if g not in groups: recs[g] = dict(recs[g], members=0)
            h.load_record(g, recs[g])
        n = rng.randrange(1, 14)
        units = [random_unit(rng, rng.choice(groups + [0])) for _ in range(n)]
        # cluster a group so the straggler test fires
        for u in units[: n // 2]: u['fixed'] = tuple(((300 + rng.randrange(40)) << 16) for _ in range(3))
        for i, u in enumerate(units): h.load_unit(i, u)
        h.put('I', PLAYER + 0x74, UNITS); h.put('I', PLAYER + 0x78, UNITS + (n - 1) * STRIDE)
        h.events.clear(); h.call(0x51b890, ())
        theirs = [h.read_record(g) for g in range(1, 100)]
        fb = {e[1]: e[2] for e in h.events if e[0] == 'fb'}
        notified = sorted(e[1] for e in h.events if e[0] == 'notify')
        text = 'tick %d\n' % int(notify and kindp in (1, 2)) + '\n'.join(record_text(recs[g]) for g in range(1, 100))
        text += '\n%d\n' % n + '\n'.join('%d %d %s %d %d' % (u['group'], int(eligible(u)), member_text(u),
                                          u['flags'] if u['mission'] else 0, scaled(u)) for u in units)
        out = ask(text, 101)
        mine = [[int(v) for v in line.split()] for line in out[:99]]
        paced = [int(v) for v in out[99].split()]
        mine_notified = sorted(int(v) for v in out[100].split())
        for g in range(99):
            a_ = theirs[g]; b_ = mine[g]
            # retail returns record fields as stored; ours mirrors them
            if a_ != b_:
                raise AssertionError(('tick', case, g + 1, notify, kindp, notified, mine_notified, a_, b_))
        want_paced = [fb.get(i, 0) for i in range(n)]
        if want_paced != paced: raise AssertionError(('paced', case, want_paced, paced))
        if notified != mine_notified: raise AssertionError(('notify', case, notified, mine_notified))
        counts['tick'] += 1; stats['notified'] += len(notified)
        stats['slowed'] += sum(1 for g in groups if theirs[g - 1][2] not in (0, recs[g]['gs']))
    # 51c700 / 51ce40 / 51d1e0
    for case in range(a.cases * 10):
        rec = random_record(rng, spread=500); rec['active'] = rng.random() < 0.9
        g = rng.randrange(1, 99); h.load_record(g, rec)
        u = random_unit(rng, g, spread=500); addr = h.load_unit(0, u)
        aa, bb, level = rng.randrange(2), rng.randrange(2), rng.randrange(0, 7)
        h.call(0x51c700, (HEAP + 0xb0000, addr, aa, bb))
        cx, cy, cz = (s16(v >> 16) for v in h.get('3i', HEAP + 0xb0000))
        radius = h.p.call(0x51ce40, (addr, aa, bb))[0]
        slot = h.p.call(0x51d1e0, (addr, bb, level))[0]
        allf = int(bool(bb and u['mission'] and u['flags'] & 0x4000000))
        mine = ask('query %s %s %d %d %d %d' % (record_text(rec), member_text(u), aa, bb, level, allf))[0].split()
        want = [cx, cy, cz, radius, slot]
        if [int(v) for v in mine] != want:
            raise AssertionError(('query', case, want, mine, rec, u, aa, bb, level))
        counts['query'] += 1
    # 402b00 head
    for case in range(a.cases * 5):
        rec = random_record(rng, spread=400); rec['active'] = True; g = rng.randrange(1, 99)
        h.load_record(g, rec)
        u = random_unit(rng, g, spread=400); u['mover'] = True; u['mission'] = True
        u['flags'] = 0x3000400 | (0x8000000 if rng.random() < 0.5 else 0); u['ustate'] = 0x1000000
        u['fixed'] = (u['fixed'][0], 0, u['fixed'][2]); addr = h.load_unit(0, u)
        ms = MISSIONS; gx, gz = rng.randrange(0, 400) << 16, rng.randrange(0, 400) << 16
        for k, v in zip((0x22, 0x26, 0x2a), (gx, 0, gz)): h.put('i', ms + k, v)
        h.put('i', ms + 0x52, 0); draw = rng.randrange(4); h.draws = [draw]; h.events.clear()
        h.call(0x402b00, (addr, ms, 0))
        flags_after = h.get('I', ms + 0x5a)[0]
        kinds = [e[0] for e in h.events]
        if 'cancel' in kinds: want = 1
        elif 'formation' in kinds: want = [e for e in h.events if e[0] == 'formation'][0][2]
        else: want = 0
        draws = kinds.count('rand')
        rec_after = h.read_record(g)
        out = ask('check %s %s %d %d 0 %d %d 0 %d %d' % (record_text(rec), member_text(u), u['flags'],
                                                       u['fixed'][0], u['fixed'][2], gx, gz, draw), 2)
        r, f, d = (int(v) for v in out[0].split()); rmine = [int(v) for v in out[1].split()]
        if (r, f, d) != (want, flags_after, draws) or (want == 1 and rmine != rec_after):
            raise AssertionError(('check', case, (want, hex(flags_after), draws, rec_after), (r, hex(f), d, rmine)))
        key = {-2: 'reform', 4: 'wait', 1: 'cancel'}.get(want)
        if key: stats[key] += 1
        counts['check'] += 1
    # 402880 Move_Ground_Formation
    for case in range(a.cases * 5):
        rec = random_record(rng, spread=400); rec['active'] = rng.random() < 0.95; g = rng.randrange(1, 99)
        h.load_record(g, rec)
        u = random_unit(rng, g, spread=400); u['mover'] = True; u['mission'] = True
        u['flags'] = rng.choice([0x11000000, 0x15000400]); addr = h.load_unit(0, u)
        ms = MISSIONS; stage = rng.randrange(0, 4); level = rng.choice([-2, -1, 2, 3, 4, 5])
        mode = rng.choice([0, 0, 1]); draw = rng.randrange(5)
        h.uc.mem_write(ms + 5, bytes([stage])); h.put('i', ms + 0x56, level); h.put('i', ms + 0x52, mode)
        h.put('I', addr + 0x130, 0x1000000 | rng.choice([0, 0x10000, 0x20000]))
        h.put('I', addr + 0xa8, 0); h.draws = [draw]; h.events.clear()
        result = h.p.call(0x402880, (addr, ms, 0), ecx=None)[0]
        stage_after = h.get('B', ms + 5)[0]
        calls = []
        for e in h.events:
            if e[0] in ('stop', 'move', 'rand'): calls.append(e[0])
            elif e[0] == 'sleep': calls.append('sleep%d' % e[2])
        gx = s16(h.get('i', ms + 0x22)[0] >> 16); gz = s16(h.get('i', ms + 0x2a)[0] >> 16)
        out = ask('formation %s %s %d %d 0 %d %d %d' % (record_text(rec), member_text(u), stage, level, mode,
                                                        u['flags'], draw))[0].split()
        r, st = int(out[0]), int(out[1])
        mine_calls = [c for c in out[4].split(',') if c and c not in ('-', 'centre', 'scan')]
        ok = (r, st) == (result, stage_after) and mine_calls == calls
        if 'centre' in out[4]: ok = ok and (int(out[2]), int(out[3])) == (gx, gz)
        if not ok:
            raise AssertionError(('formation', case, dict(stage=stage, level=level, mode=mode),
                                  (result, stage_after, gx, gz, calls), out))
        counts['formation'] += 1
    ours.stdin.close(); ours.wait()
    print('PASS: %s; %s' % (', '.join('%s %d' % kv for kv in counts.items()),
                            ', '.join('%s %d' % kv for kv in stats.items())))


if __name__ == '__main__': main()
