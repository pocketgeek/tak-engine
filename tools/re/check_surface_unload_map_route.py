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


def check_route(world_binary, retail_root, map_name, start_cell, target_cell, footprint,
                carrier=None, passenger=None, crusades=False):
    env = os.environ.copy()
    env['TAK_DUMP_GRADE_PLANE'] = '1'
    if carrier:
        env['TAK_DUMP_ATTEMPT_PLANES'] = '1'
        env['TAK_DUMP_ROUTE_ATTEMPT'] = '1'
        command = [world_binary, '--surface-unload-map-route-type', retail_root,
                   map_name, carrier, passenger, str(start_cell[0]), str(start_cell[1]),
                   str(target_cell[0]), str(target_cell[1]), str(int(crusades))]
    else:
        command = [world_binary, '--surface-unload-map-route', retail_root, map_name,
                   str(start_cell[0]), str(start_cell[1]), str(target_cell[0]),
                   str(target_cell[1]), str(footprint)]
    world = subprocess.run(command, check=True, capture_output=True, text=True, env=env)
    stderr = world.stderr.splitlines()
    profile = next(line for line in stderr if line.startswith('COSTPROFILE ')).split()
    turn, fx, fz, road, water, flags, cost_heavy, heading = map(int, profile[1:])
    completed_attempt = None
    if carrier:
        profiles = {}
        for line in stderr:
            if line.startswith('ATTEMPTPROFILE '):
                v = list(map(int, line.split()[1:]))
                tick, unit_id, attempt_turn, attempt_fx, attempt_fz, attempt_road, \
                    attempt_water, attempt_flags, transport_dist, max_water, min_water, \
                    half_cell_ticks, attempt_heavy, _last_retry = v
                profiles[(tick, unit_id)] = (attempt_turn, attempt_fx, attempt_fz,
                    attempt_road, attempt_water, attempt_flags, transport_dist,
                    max_water, min_water, half_cell_ticks, attempt_heavy)
        attempts = [(i, list(map(int, line.split()[1:])))
                    for i, line in enumerate(stderr)
                    if line.startswith('ATTEMPTPLANE ')]
        completed_attempt = next(list(map(int, line.split()[1:])) for line in stderr
                                 if line.startswith('WORLDATTEMPT '))
        (attempt_tick, attempt_unit, sx, sz, attempt_goal_x, attempt_goal_z,
         heading, retry, weight, initial_distance, partial_distance, phase,
         endpoint, route_flags, ground_cost, road_cost, slope_cost, traffic_cost,
         short_turn_cost, min_straight_cost, attempt_heavy, raw_count) = completed_attempt
        matching = [(i, h) for i, h in attempts
                    if h[0] == attempt_tick and h[1] == attempt_unit and
                    (h[2], h[3], h[4], h[5]) == (sx, sz, retry, heading)]
        assert len(matching) == 1, (completed_attempt, [h for _, h in attempts])
        header_index, plane = matching[0]
        (plane_tick, plane_unit, plane_sx, plane_sz, plane_retry, plane_heading,
         plane_weight, traffic_radius, width, height, grade_fx, grade_fz) = plane
        assert (plane_tick, plane_unit, plane_sx, plane_sz, plane_retry, plane_heading,
                plane_weight) == (attempt_tick, attempt_unit, sx, sz, retry, heading, weight)
        type_profile = profiles[(attempt_tick, attempt_unit)]
        (turn, fx, fz, road, water, flags, _transport_dist, max_water, min_water,
         half_cell_ticks, attempt_heavy_profile) = type_profile
        assert (fx, fz, attempt_heavy_profile) == (grade_fx, grade_fz, attempt_heavy)
        assert traffic_radius == 50 // half_cell_ticks, \
            (traffic_radius, half_cell_ticks)
        assert (ground_cost, road_cost, slope_cost, traffic_cost, short_turn_cost,
                min_straight_cost) == (24, 8, 320, 80, 680, 14), completed_attempt
        grades = [int(value) for line in stderr[header_index + 1:header_index + 1 + height]
                  for value in line.split()]
        assert len(grades) == width * height
        world_raw = [tuple(map(int, line.split()[2:])) for line in stderr
                     if line.startswith('WORLDRAW ')]
        assert len(world_raw) == raw_count and world_raw[0] == (sx, sz), \
            (world_raw, completed_attempt)
    else:
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

    transport_profile = next((line for line in stderr
                              if line.startswith('TRANSPORTPROFILE ')), None)
    if transport_profile:
        transport_dist, max_water, min_water, heavy = map(
            int, transport_profile.split()[1:])
        assert transport_dist > 34, transport_profile
        assert heavy == cost_heavy, (transport_profile, profile)
        circle_radius = transport_dist - 34
    else:
        max_water, min_water, heavy = 10000, 13, 1
        circle_radius = 116

    stdout = world.stdout.splitlines()
    map_header = next(line for line in stdout if line.startswith('MAPROUTE ')).split()
    map_width, map_height, sea, map_foot, start_x, start_z, target_x, target_z, _unit_id = \
        map(int, map_header[1:])
    if carrier:
        assert (map_width, map_height, map_foot, fx, fz) == (width, height, fx, fx, fz)
    else:
        assert (map_width, map_height, map_foot, fx, fz) == \
            (width, height, footprint, footprint, footprint)
    route_index = next(i for i, line in enumerate(stdout) if line.startswith('ROUTE '))
    route_header = list(map(int, stdout[route_index].split()[1:]))
    route_tick, _, _, anchor_x, anchor_z, route_count, completions, failures = route_header
    assert completions + failures > 0, route_header
    anchor = (anchor_x // 65536, anchor_z // 65536)
    world_points_fixed = [tuple(map(int, line.split()))
                          for line in stdout[route_index + 1:route_index + 1 + route_count]]
    world_route = [(x // 65536, z // 65536) for x, z in world_points_fixed]
    if transport_profile and world_route:
        dx = world_route[-1][0] - target_x
        dz = world_route[-1][1] - target_z
        assert dx * dx + dz * dz <= circle_radius * circle_radius, \
            (world_route[-1], (target_x, target_z), circle_radius)

    p = Phase(width, height)
    unit = p.unit(sx - fx // 2, sz - fz // 2)
    assert p.construct() is None
    mover = struct.unpack('<I', p.uc.mem_read(unit + 8, 4))[0]
    goal_cell = ((target_x - (fx - 1) * 8) // 16,
                 (target_z - (fz - 1) * 8) // 16)
    p.plant_request(unit, (sx - fx // 2, sz - fz // 2), goal_cell)
    if carrier:
        p.uc.mem_write(unit + 0x68, struct.pack('<iii', route_header[1],
                                               sea * 65536, route_header[2]))
    else:
        p.uc.mem_write(unit + 0x68, struct.pack('<iii', start_x * 65536,
                                               sea * 65536, start_z * 65536))
    p.uc.mem_write(unit + 0x78, struct.pack('<hh', fx, fz))
    p.uc.mem_write(unit + 0x7e, struct.pack('<H', heading))
    p.uc.mem_write(mover + 0x36, struct.pack('<H', flags))
    p.uc.mem_write(TYPE + 0x126, struct.pack('<hh', fx, fz))
    p.uc.mem_write(TYPE + 0x18e, struct.pack('<H', turn))
    p.uc.mem_write(TYPE + 0x172, struct.pack('<i', road))
    p.uc.mem_write(TYPE + 0x260, struct.pack('<I', 0x80000 if heavy else 0))
    p.uc.mem_write(TYPE + 0x16e, struct.pack('<i', water))
    p.uc.mem_write(TYPE + 0x192, struct.pack('<hh', max_water, min_water))
    if carrier:
        p.uc.mem_write(TYPE + 0x249, bytes([half_cell_ticks]))
        p.uc.mem_write(OBJ + 0x1ad, struct.pack('<I', retry))
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
        (p.HANDLE + 0x1000, target_x * 65536, target_z * 65536, circle_radius), ecx=p.HANDLE)
    assert error is None, error
    native_goal = tuple(struct.unpack('<hh', p.uc.mem_read(p.HANDLE + 8, 4)))
    native_radius, radius_squared = struct.unpack('<ii', p.uc.mem_read(p.HANDLE + 0x0c, 8))
    expected_radius_squared = int(circle_radius * circle_radius / 256 + 0.5)
    assert (native_goal, native_radius, radius_squared) == \
        (goal_cell, circle_radius, expected_radius_squared), \
        (native_goal, goal_cell, native_radius, radius_squared)
    _, error = p.init()
    assert error is None, error
    p.uc.mem_write(OBJ + 0x165, struct.pack('<I', 10_000_000))
    p.uc.mem_write(OBJ + 0x5c, struct.pack('<I', 1))
    _, error = p.step()
    assert error is None, error
    # 415b10 can deliver a direct trace route on this first handoff. It leaves
    # +0x5c at the phase-2 value, but no cost heap is seeded in that case; calling
    # 4142c0 anyway reads a null +4 pointer at 414367. Treat the actual callback
    # as completion before advancing the state machine again.
    completed = bool(native_routes)
    if not completed:
        assert p.phase() == 2, 'retail reachability tracer rejected the connected map route'
        assert struct.unpack('<I', p.uc.mem_read(OBJ + 4, 4))[0] != 0, \
            'phase-2 handoff did not initialize retail cost-search heap'
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
    if carrier:
        world_raw_pixels = [(x * 16, z * 16) for x, z in world_raw]
        assert native_route == world_raw_pixels, (native_route, world_raw_pixels)
        assert world_route == world_raw_pixels[1:], (world_route, world_raw_pixels)
        assert (attempt_goal_x - fx // 2, attempt_goal_z - fz // 2) == goal_cell, \
            ((attempt_goal_x, attempt_goal_z), goal_cell)
        assert p.get(0x54) == weight, ('retail initial path weight', p.get(0x54), weight)
    else:
        assert native_route[0] == anchor, (native_route[0], anchor)
        assert native_route[1:] == world_route, (native_route[1:], world_route)
    profile_name = f' {carrier}/{passenger}' if carrier else ''
    arrival = (f', endpoint inside {circle_radius}px unload circle'
               if transport_profile and world_route else '')
    print(f'PASS: {map_name}{profile_name} unload route matches exactly at World tick '
          f'{route_tick}; {len(world_route)} waypoints{arrival}, {query_count} native '
          f'grade queries, {completions} World completion(s), {failures} World failure(s)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('world_binary', nargs='?', default='build-o2/transport_test')
    parser.add_argument('--retail-root', default='/home/pocket_geek/tak_data')
    parser.add_argument('--map', default='Per Mare Per Terras')
    parser.add_argument('--start', nargs=2, type=int, default=(40, 120), metavar=('X', 'Z'))
    parser.add_argument('--target', nargs=2, type=int, default=(40, 142), metavar=('X', 'Z'))
    parser.add_argument('--footprint', type=int, default=4)
    parser.add_argument('--carrier', help='use an asset-backed carrier profile instead of the synthetic boat')
    parser.add_argument('--passenger', help='land passenger profile required with --carrier')
    parser.add_argument('--crusades', action='store_true', help='load Crusades unit balance')
    args = parser.parse_args()
    if bool(args.carrier) != bool(args.passenger):
        parser.error('--carrier and --passenger must be supplied together')
    check_route(args.world_binary, args.retail_root, args.map, tuple(args.start),
                tuple(args.target), args.footprint, args.carrier, args.passenger,
                args.crusades)


if __name__ == '__main__':
    main()
