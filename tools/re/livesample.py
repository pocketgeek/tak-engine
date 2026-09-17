#!/usr/bin/env python3
"""Read the LIVE retail game's state out of a running KINGDOMS.icd (under Wine/Proton).

KINGDOMS.icd is non-relocatable at ImageBase 0x400000 (no .reloc, no DYNAMICBASE),
so every VA from static RE / the unicorn harness is valid verbatim in the running
process's address space. We read it through /proc/<pid>/mem -- the same window
scanmem/Cheat Engine use on Wine games. OBSERVATION ONLY: nothing is written, and
nothing from the binary is copied into the engine. This is the "emulate a routine
to check a port" method (docs/retail-engine.md), extended to the whole live process
so the pacing + pathfinding residuals can be ground-truthed against the real game.

  livesample.py [pid]            auto-finds the Kingdoms process if pid omitted
  livesample.py [pid] --dump N   hexdump the first N live unit slots (to find fields)
  livesample.py [pid] --watch    stream unit (id,x,z,heading) at ~20 Hz

Addresses (VA, ImageBase 0x400000), from tools/re/emuphase.py:
  [0x62d55c]        -> gamestate base G
  G+0x19e98/0x19e9c -> map W / H (cells)
  G+0x19edc         -> unit array base   (stride 0x140)
  G+0x19ec0         -> unit slot count
"""
import sys, struct, glob, os, time

GAMESTATE_PTR = 0x62d55c
OFF_W, OFF_H  = 0x19e98, 0x19e9c
OFF_UNITS     = 0x19edc
OFF_NUNITS    = 0x19ec0
STRIDE        = 0x140


def find_pid():
    for d in glob.glob('/proc/[0-9]*'):
        try:
            cl = open(d + '/cmdline', 'rb').read().replace(b'\0', b' ').lower()
        except OSError:
            continue
        if b'kingdoms' in cl and b'.icd' not in cl.split(b' ')[0]:
            # the wine process launched with Kingdoms.exe / KINGDOMS.icd
            if b'kingdoms.exe' in cl or b'kingdoms.icd' in cl or b'\\kingdoms' in cl:
                return int(os.path.basename(d))
    # fallback: any process with KINGDOMS.icd mapped
    for d in glob.glob('/proc/[0-9]*'):
        try:
            if b'KINGDOMS' in open(d + '/maps', 'rb').read().upper():
                return int(os.path.basename(d))
        except OSError:
            continue
    return None


class Mem:
    def __init__(self, pid):
        self.pid = pid
        self.f = open('/proc/%d/mem' % pid, 'rb', 0)

    def read(self, va, n):
        self.f.seek(va)
        return self.f.read(n)

    def u32(self, va):
        b = self.read(va, 4)
        return struct.unpack('<I', b)[0] if len(b) == 4 else None

    def i16(self, va):
        b = self.read(va, 2)
        return struct.unpack('<h', b)[0] if len(b) == 2 else None


def gamestate(m):
    g = m.u32(GAMESTATE_PTR)
    if not g:
        return None
    return g


def summary(m):
    g = gamestate(m)
    if not g:
        print("gamestate ptr [%#x] is null -- no match in progress yet" % GAMESTATE_PTR)
        return None
    w, h = m.u32(g + OFF_W), m.u32(g + OFF_H)
    units = m.u32(g + OFF_UNITS)
    n = m.u32(g + OFF_NUNITS)
    print("pid=%d G=%#x map=%sx%s units@%#x slots=%s" % (m.pid, g, w, h, units, n))
    return g, w, h, units, n


def dump(m, count):
    s = summary(m)
    if not s: return
    g, w, h, units, n = s
    for i in range(min(count, n or count)):
        base = units + i * STRIDE
        blk = m.read(base, STRIDE)
        if len(blk) < STRIDE: break
        # only print slots that look occupied (nonzero in first 0x20)
        if blk[:0x20] == b'\0' * 0x20: continue
        print("--- slot %d @ %#x ---" % (i, base))
        for r in range(0, STRIDE, 16):
            row = blk[r:r+16]
            hexs = ' '.join('%02x' % c for c in row)
            print("  +%03x  %s" % (r, hexs))


def watch(m, off_x, off_z, off_hd):
    s = summary(m)
    if not s: return
    g, w, h, units, n = s
    print("t,slot,id,x,z,heading")
    t0 = time.time()
    while True:
        g = gamestate(m)
        if not g: break
        units = m.u32(g + OFF_UNITS); n = m.u32(g + OFF_NUNITS) or 0
        t = time.time() - t0
        for i in range(n):
            base = units + i * STRIDE
            uid = m.i16(base + 0x2)
            if uid is None: continue
            x = m.u32(base + off_x); z = m.u32(base + off_z)
            if x is None or z is None: continue
            hd = m.i16(base + off_hd)
            # skip empty slots
            if uid == 0 and x == 0 and z == 0: continue
            print("%.3f,%d,%d,%d,%d,%s" % (t, i, uid, x, z, hd))
        time.sleep(0.05)


def main():
    args = sys.argv[1:]
    pid = None
    if args and args[0].isdigit():
        pid = int(args[0]); args = args[1:]
    if pid is None:
        pid = find_pid()
    if pid is None:
        print("no running Kingdoms process found (launch the game first)"); return 2
    m = Mem(pid)
    if args and args[0] == '--dump':
        dump(m, int(args[1]) if len(args) > 1 else 16)
    elif args and args[0] == '--watch':
        # position offsets passed as --watch X Z HD (hex), default guesses refined empirically
        ox = int(args[1], 0) if len(args) > 1 else 0x0c
        oz = int(args[2], 0) if len(args) > 2 else 0x10
        oh = int(args[3], 0) if len(args) > 3 else 0x78
        watch(m, ox, oz, oh)
    else:
        summary(m)
    return 0


if __name__ == '__main__':
    sys.exit(main())
