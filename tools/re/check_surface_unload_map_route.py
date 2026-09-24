#!/usr/bin/env python3
"""Compare a boat unload route on a shipped map with retail's native search.

The C++ fixture loads the real TNT terrain and feature plane into World, then
exports the effective grades from the actual unload-circle request. Unicorn runs
retail's original circle constructor, reachability tracer, cost search and route
reconstruction against those same grades. This verifies route choice on real map
geometry; it does not claim that World and retail independently generated identical
grade planes.
"""
import argparse
import os
import struct
import subprocess

from emuphase import Phase, OBJ, TYPE


def check_route(world_binary, retail_root, map_name, start_cell, target_cell, footprint):
    env = os.environ.copy()
    env['TAK_DUMP_GRADE_PLANE'] = '1'
    command = [world_binary, '--surface-unload-map-route', retail_root, map_name,
               str(start_cell[0]), str(start_cell[1]), str(target_cell[0]),
               str(target_cell[1]), str(footprint)]
    world = subprocess.run(command, check=True, capture_output=True, text=True, env=env)
    stderr = world.stderr.splitlines()
    profile = next(line for line in stderr if line.startswith('COSTPROFILE ')).split()
    turn, fx, fz, road, water, flags, heavy, heading = map(int, profile[1:])
    header_index = next(i for i, line in enumerate(stderr) if line.startswith('GRADEPLANE '))
    width, height, grade_fx, grade_fz, retry, sx, sz, tick, grade_heading = map(
        int, stderr[header_index].split()[1:])
    assert (grade_fx, grade_fz, retry, grade_heading) == (fx, fz, 0, heading)
    grades = [int(value) for line in stderr[header_index + 1:header_index + 1 + height]
              for value in line.split()]
    assert len(grades) == width * height
    assert not any(line.startswith('GRADECHANGE ') for line in
                   stderr[header_index + 1 + height:]), \
        'the effective grade plane changed during this search; static comparison is invalid'

    stdout = world.stdout.splitlines()
    map_header = next(line for line in stdout if line.startswith('MAPROUTE ')).split()
    map_width, map_height, sea, map_foot, start_x, start_z, target_x, target_z, _unit_id = \
        map(int, map_header[1:])
    assert (map_width, map_height, map_foot, fx, fz) == (width, height, footprint, footprint, footprint)
    route_index = next(i for i, line in enumerate(stdout) if line.startswith('ROUTE '))
    route_header = list(map(int, stdout[route_index].split()[1:]))
    route_tick, _, _, anchor_x, anchor_z, route_count, completions, failures = route_header
    assert completions + failures > 0, route_header
    anchor = (anchor_x // 65536, anchor_z // 65536)
    world_points_fixed = [tuple(map(int, line.split()))
                          for line in stdout[route_index + 1:route_index + 1 + route_count]]
    world_route = [(x // 65536, z // 65536) for x, z in world_points_fixed]

    p = Phase(width, height)
    unit = p.unit(sx - fx // 2, sz - fz // 2)
    assert p.construct() is None
    mover = struct.unpack('<I', p.uc.mem_read(unit + 8, 4))[0]
    goal_cell = ((target_x - (fx - 1) * 8) // 16,
                 (target_z - (fz - 1) * 8) // 16)
    p.plant_request(unit, (sx - fx // 2, sz - fz // 2), goal_cell)
    p.uc.mem_write(unit + 0x68, struct.pack('<iii', start_x * 65536,
                                           sea * 65536, start_z * 65536))
    p.uc.mem_write(unit + 0x78, struct.pack('<hh', fx, fz))
    p.uc.mem_write(unit + 0x7e, struct.pack('<H', heading))
    p.uc.mem_write(mover + 0x36, struct.pack('<H', flags))
    p.uc.mem_write(TYPE + 0x126, struct.pack('<hh', fx, fz))
    p.uc.mem_write(TYPE + 0x18e, struct.pack('<H', turn))
    p.uc.mem_write(TYPE + 0x172, struct.pack('<i', road))
    p.uc.mem_write(TYPE + 0x260, struct.pack('<I', 0x80000))
    p.uc.mem_write(TYPE + 0x16e, struct.pack('<i', water))
    p.uc.mem_write(TYPE + 0x192, struct.pack('<hh', 10000, 13))
    p.uc.mem_write(p.GRID + 4, struct.pack('<hh', fx, fz))

    query_count = 0

    def grade(_uc, args):
        nonlocal query_count
        x, z = struct.unpack('<ii', p.uc.mem_read(args, 8))
        query_count += 1
        value = grades[z * width + x] if 0 <= x < width and 0 <= z < height else 0
        return 3, value

    native_routes = []

    def receive(_uc, args):
        address, count = struct.unpack('<II', p.uc.mem_read(args, 8))
        points = struct.unpack('<' + 'h' * (count * 2),
                               p.uc.mem_read(address, count * 4)) if count else ()
        native_routes.append(list(zip(points[::2], points[1::2])))
        return 2, 0

    p.icd.hooks[0x4139d0] = grade
    p.icd.hooks[0x4e4ea0] = receive
    _, error = p.icd.call(0x4e2500,
        (p.HANDLE + 0x1000, target_x * 65536, target_z * 65536, 116), ecx=p.HANDLE)
    assert error is None, error
    native_goal = tuple(struct.unpack('<hh', p.uc.mem_read(p.HANDLE + 8, 4)))
    radius, radius_squared = struct.unpack('<ii', p.uc.mem_read(p.HANDLE + 0x0c, 8))
    assert (native_goal, radius, radius_squared) == (goal_cell, 116, 53), \
        (native_goal, goal_cell, radius, radius_squared)
    _, error = p.init()
    assert error is None, error
    p.uc.mem_write(OBJ + 0x165, struct.pack('<I', 10_000_000))
    p.uc.mem_write(OBJ + 0x5c, struct.pack('<I', 1))
    _, error = p.step()
    assert error is None, error
    assert p.phase() == 2, 'retail reachability tracer rejected the connected map route'
    completed = False
    for step in range(100_000):
        value, error = p.step()
        assert error is None, (step, error)
        if value:
            completed = True
            _, error = p.icd.call(0x414450, (0,), ecx=OBJ)
            assert error is None, error
            break
    assert completed and len(native_routes) == 1, (completed, len(native_routes))
    native_route = native_routes[0]
    assert native_route[0] == anchor, (native_route[0], anchor)
    assert native_route[1:] == world_route, (native_route[1:], world_route)
    print(f'PASS: {map_name} boat unload route matches exactly at World tick {route_tick}; '
          f'{len(world_route)} waypoints, {query_count} native grade queries, '
          f'{completions} World completion(s), {failures} World failure(s)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('world_binary', nargs='?', default='build-o2/transport_test')
    parser.add_argument('--retail-root', default='/home/pocket_geek/tak_data')
    parser.add_argument('--map', default='Per Mare Per Terras')
    parser.add_argument('--start', nargs=2, type=int, default=(40, 120), metavar=('X', 'Z'))
    parser.add_argument('--target', nargs=2, type=int, default=(40, 142), metavar=('X', 'Z'))
    parser.add_argument('--footprint', type=int, default=4)
    args = parser.parse_args()
    check_route(args.world_binary, args.retail_root, args.map, tuple(args.start),
                tuple(args.target), args.footprint)


if __name__ == '__main__':
    main()
