#!/usr/bin/env python3
"""Drive retail's full pathfinding lifecycle under emulation and OBSERVE the route.

Constructs the real search object (0x415f80), plants a request (grid, unit,
start, goal), runs init (0x415170), then steps the scheduler's phase machine
(+0x5c): phase 1 = tracer (0x4146e0 via 0x415b10), phase 2 = best-first cost
search (0x4142c0), until a route is produced. Observation only.
"""
import sys, struct
sys.path.insert(0, "/home/pocket_geek/TAK/tools/re")
from emu import Icd, HEAP
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EIP

ARENA = 0x40000000
ARENA_SZ = 0x8000000
GS   = ARENA + 0x0800000
CELLS= ARENA + 0x0900000
UNITS= ARENA + 0x0A00000
TYPE = ARENA + 0x0B00000
OBJ  = ARENA + 0x0C00000
BRK0 = ARENA + 0x1000000


class Phase:
    def __init__(self, W=24, H=24):
        self.icd = Icd()
        self.uc = self.icd.uc
        self.uc.mem_map(ARENA, ARENA_SZ)
        self.W, self.H = W, H
        self.blocked = set()
        self.brk = BRK0
        self._install_shims()
        self._gamestate()

    def _alloc(self, n):
        if n > 0x400000:
            raise RuntimeError("bogus alloc size %#x (brk=%#x)" % (n, self.brk))
        a = self.brk
        self.brk = (a + max(n, 8) + 15) & ~15
        self.uc.mem_write(a, b"\0" * max(n, 8))
        return a

    def _install_shims(self):
        icd = self.icd

        def one_arg_alloc(uc, argp):
            n = struct.unpack("<I", uc.mem_read(argp, 4))[0]
            return (1, self._alloc(n))

        def two_arg_alloc(uc, argp):
            n = struct.unpack("<I", uc.mem_read(argp + 4, 4))[0]
            return (2, self._alloc(n))

        icd.hooks[0x4eba00] = lambda uc, argp: (1, 0)   # free(ptr) -> no-op
        icd.hooks[0x4eb9e0] = one_arg_alloc             # malloc(size)
        icd.hooks[0x5ba3d0] = two_arg_alloc             # alloc(tag, size)
        icd.hooks[0x5d3d12] = lambda uc, argp: (1, 0)   # atexit
        icd.hooks[0x535cc0] = lambda uc, argp: (1, 0)   # rand(n) -> 0

    def _gamestate(self):
        uc = self.uc
        uc.mem_write(0x62d55c, struct.pack("<I", GS))
        uc.mem_write(0x62d558, struct.pack("<I", GS + 0x600000))
        uc.mem_write(GS, b"\0" * 0x30000)
        g = lambda off, v: uc.mem_write(GS + off, struct.pack("<I", v))
        g(0x19e98, self.W)
        g(0x19e9c, self.H)
        g(0x19f04, CELLS)
        g(0x19edc, UNITS)
        g(0x19ec0, 64)
        uc.mem_write(GS + 0x19ef8, bytes([0]))
        # coarse region bitmap read by 0x413c80 -- all zero (clear)
        BM = GS + 0x100000
        uc.mem_write(GS + 0x19ef4, struct.pack("<I", BM))
        uc.mem_write(BM, b"\0" * 0x40000)
        cell = bytearray(14)
        cell[8:10] = b"\xff\xff"
        uc.mem_write(CELLS, bytes(cell) * (self.W * self.H))
        uc.mem_write(UNITS, b"\0" * (0x140 * 64))
        uc.mem_write(TYPE, b"\0" * 0x400)

    def unit(self, cellx, cellz):
        uc = self.uc
        u = UNITS + 1 * 0x140
        uc.mem_write(u, b"\0" * 0x140)
        uc.mem_write(u + 2, struct.pack("<H", 1))
        uc.mem_write(u + 0xb4, struct.pack("<I", TYPE))
        uc.mem_write(u + 0x78, struct.pack("<h", 0))
        uc.mem_write(u + 8, struct.pack("<I", u + 0x300))
        uc.mem_write(u + 0x300 + 0x36, struct.pack("<H", 0))
        t = TYPE
        uc.mem_write(t + 0x249, bytes([6]))
        uc.mem_write(t + 0x18e, struct.pack("<H", 30))
        uc.mem_write(t + 0x172, struct.pack("<i", 0x13333))
        uc.mem_write(t + 0x16e, struct.pack("<i", 0x10000))
        # player object (unit+0xb8), +0xeb = player index 0
        PLR = ARENA + 0x0E00000
        uc.mem_write(PLR, b"\0" * 0x400)
        uc.mem_write(PLR + 0xeb, bytes([0]))
        uc.mem_write(u + 0xb8, struct.pack("<I", PLR))
        self.U = u
        return u

    def construct(self):
        self.uc.mem_write(OBJ, b"\0" * 0x400)
        return self.icd.call(0x415f80, args=(), ecx=OBJ)[1]

    def plant_request(self, unit, start, goal):
        uc = self.uc
        uc.mem_write(unit + 0x74, struct.pack("<hh", goal[0], goal[1]))
        uc.mem_write(OBJ + 0x58, struct.pack("<I", unit))
        H  = ARENA + 0x0D00000
        uc.mem_write(H, b"\0" * 0x200)
        VT = ARENA + 0x0D01000
        PT = ARENA + 0x0D03000
        STUBS = ARENA + 0x0D04000    # one RET per vtable slot
        uc.mem_write(H, struct.pack("<I", VT))
        uc.mem_write(PT, struct.pack("<hh", start[0], start[1]))
        uc.mem_write(OBJ + 0x68, struct.pack("<I", H))
        self.HANDLE, self.START_PT, self.GOAL = H, PT, goal
        # a vtable of 12 slots, each pointing at its own RET we hook
        for slot in range(12):
            addr = STUBS + slot * 0x10
            uc.mem_write(addr, b"\xc3")
            uc.mem_write(VT + slot * 4, struct.pack("<I", addr))
        gx, gz = goal

        def slot5(uc, argp):   # (+0x14)(x, z) -> reject predicate (0 = accept)
            return (2, 0)

        def slot6(uc, argp):   # (+0x18)(&out) -> start range [PT, PT+4)
            out = struct.unpack("<I", uc.mem_read(argp, 4))[0]
            uc.mem_write(out + 4, struct.pack("<I", PT))
            uc.mem_write(out + 8, struct.pack("<I", PT + 4))
            return (1, 0)

        def slot7(uc, argp):   # (+0x1c)(x, z) -> cell GRADE (6 open, 0 blocked)
            x = struct.unpack("<i", uc.mem_read(argp, 4))[0]
            z = struct.unpack("<i", uc.mem_read(argp + 4, 4))[0]
            if x < 0 or z < 0 or x >= self.W or z >= self.H:
                return (2, 0)
            return (2, 0 if (x, z) in self.blocked else 6)

        # unknown-slot fallbacks: cleaning 0 args is usually wrong, so give a few
        # common counts; refine as faults reveal each slot's real arity.
        def ret0_0(uc, argp): return (0, 0)
        def ret0_1(uc, argp): return (1, 0)
        def ret0_2(uc, argp): return (2, 0)
        for slot in range(12):
            self.icd.hooks[STUBS + slot * 0x10] = ret0_1
        self.icd.hooks[STUBS + 5 * 0x10] = slot5
        self.icd.hooks[STUBS + 6 * 0x10] = slot6
        self.icd.hooks[STUBS + 7 * 0x10] = slot7
        # nav-grid object at GRID: dims at +0x340/+0x344, packed 3-bit grade map at
        # +0x348 (index = width*(z>>3)+x, grade = (dword >> (z&7)*4) & 7). Grade 6 =
        # open. This is what the tracer (0x4139d0) and cost search actually read.
        GRID = ARENA + 0x0F00000
        GMAP = ARENA + 0x0F10000
        uc.mem_write(GRID, b"\0" * 0x400)
        uc.mem_write(GRID + 4, struct.pack("<h", self.W + self.H))   # window x half-extent
        uc.mem_write(GRID + 6, struct.pack("<h", self.W + self.H))   # window z half-extent
        uc.mem_write(GRID + 0x340, struct.pack("<I", self.W))
        uc.mem_write(GRID + 0x344, struct.pack("<I", self.H))
        uc.mem_write(GRID + 0x348, struct.pack("<I", GMAP))
        rows8 = (self.H + 7) // 8
        # all grade 6 (open): each dword = 6 in all 8 nibble slots
        import array as _a
        words = _a.array("I", [0x66666666] * (self.W * rows8))
        uc.mem_write(GMAP, words.tobytes())
        self.GRID, self.GMAP, self.rows8 = GRID, GMAP, rows8
        for (bx, bz) in self.blocked:
            self._set_grade(bx, bz, 0)
        uc.mem_write(OBJ + 0x64, struct.pack("<I", H))     # route output = handle
        # give the handle route storage (+0xc..+0x10c) room -- already zeroed in H alloc
        mv = struct.unpack("<I", uc.mem_read(unit + 8, 4))[0]
        uc.mem_write(mv + 4, struct.pack("<I", GRID))
        uc.mem_write(OBJ + 0x6c, struct.pack("<I", GRID))

    def _set_grade(self, x, z, g):
        idx = self.W * (z >> 3) + x
        addr = self.GMAP + idx * 4
        import struct as _s
        d = _s.unpack("<I", self.uc.mem_read(addr, 4))[0]
        shift = (z & 7) * 4
        d = (d & ~(0x7 << shift)) | ((g & 7) << shift)
        self.uc.mem_write(addr, _s.pack("<I", d))

    def init(self):
        return self.icd.call(0x415170, args=(self.HANDLE,), ecx=OBJ)

    def phase(self):
        return struct.unpack("<i", self.uc.mem_read(OBJ + 0x5c, 4))[0]

    def step(self):
        ph = self.phase()
        if ph == 0:
            self.uc.mem_write(OBJ + 0x48, struct.pack("<i", 0x1f4))
            self.uc.mem_write(OBJ + 0x5c, struct.pack("<i", 1))
            return self.icd.call(0x415170, args=(self.HANDLE,), ecx=OBJ)
        if ph == 1:
            self.uc.mem_write(OBJ + 0x5c, struct.pack("<i", 2))
            return self.icd.call(0x415b10, args=(), ecx=OBJ)
        if ph == 2:
            return self.icd.call(0x4142c0, args=(), ecx=OBJ)
        return (None, "unknown phase %d" % ph)

    def get(self, off, n=4):
        return struct.unpack("<i", self.uc.mem_read(OBJ + off, 4))[0] if n == 4 else \
               struct.unpack("<h", self.uc.mem_read(OBJ + off, 2))[0]


if __name__ == "__main__":
    p = Phase()
    u = p.unit(2, 2)
    print("construct:", "OK" if p.construct() is None else "FAIL")
    p.plant_request(u, start=(2, 2), goal=(20, 20))
    trace = []

    def tr(uc, addr, size, _):
        trace.append(addr)
        if len(trace) > 80:
            trace.pop(0)

    p.uc.hook_add(UC_HOOK_CODE, tr)
    v, e = p.init()
    print("init:", "OK" if e is None else e)
    if e:
        print("last EIPs:", " ".join("%08x" % a for a in trace[-24:]))
    else:
        for off in (0x30, 0x34, 0xbc, 0xc0, 0xc4, 0xc8, 0xb4, 0xb8, 0xec, 0x5c):
            print("  OBJ+%#x = %#x" % (off, p.get(off) & 0xffffffff))
