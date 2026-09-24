#!/usr/bin/env python3
"""Compare a boat unload route on a shipped map with retail's native search.

The C++ fixture loads the real TNT terrain and feature plane into World, then
exports the effective grades from the actual unload-circle request. Unicorn runs
retail's original circle constructor, reachability tracer, cost search and route
reconstruction against either those grades or native 0x508cd0 grades generated
from the TNT cell plane. An optional mover trace steps both implementations over
the shipped map without launching a game GUI.
An optional live-unload composition keeps the native mission and cargo active
through map-backed movement, arrival wakeup, passenger placement and release.
"""
import argparse
import os
import struct
import subprocess
from pathlib import Path

from emuphase import Phase, OBJ, TYPE, GS
from check_surface_unload_map_grades import native_grade_reader, native_water_profile
from check_surface_unload_map_release import (
    cat, movement_profile, native_placement_oracle, parse_tnt)


def check_route(world_binary, retail_root, map_name, start_cell, target_cell, footprint,
                carrier=None, passenger=None, crusades=False, native_map_grades=False,
                hpitool='build/hpitool', native_map_mover_steps=0,
                native_live_unload=False):
    if native_live_unload and (not carrier or not native_map_mover_steps):
        raise ValueError('--native-live-unload requires a carrier and map mover steps')
    if native_live_unload and (map_name.lower() != 'lake lokken' or
                               carrier.lower() != 'vertrans' or
                               passenger.lower() != 'araarch'):
        raise ValueError('--native-live-unload currently checks Lake Lokken Vertrans/Araarch')
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
    if native_map_mover_steps:
        env['TAK_MAP_SURFACE_STEPS'] = str(native_map_mover_steps)
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
    # ROUTE includes its start/anchor entry in route_count but the fixture emits
    # only actual order waypoints. Optional mover diagnostics follow those
    # pairs, so select coordinate rows rather than consuming route_count lines.
    world_points_fixed = []
    for line in stdout[route_index + 1:]:
        fields = line.split()
        if len(fields) != 2:
            if world_points_fixed:
                break
            continue
        try:
            world_points_fixed.append(tuple(map(int, fields)))
        except ValueError:
            if world_points_fixed:
                break
    world_route = [(x // 65536, z // 65536) for x, z in world_points_fixed]
    # ROUTE includes the anchor/start entry in its reported count, while the
    # fixture prints only the following actual order waypoints.
    expected_emitted_points = route_count - 1
    assert len(world_route) == expected_emitted_points, (
        route_header, world_route, expected_emitted_points)
    world_steps = []
    world_seed = world_type = None
    if native_map_mover_steps:
        world_seed = tuple(map(int, next(line for line in stdout
                                         if line.startswith('WORLDSEED ')).split()[1:]))
        world_type = tuple(map(int, next(line for line in stdout
                                         if line.startswith('WORLDTYPE ')).split()[1:]))
        world_steps = [tuple(map(int, line.split()[1:])) for line in stdout
                       if line.startswith('WORLDSTEP ')]
        assert len(world_steps) == native_map_mover_steps, (
            len(world_steps), native_map_mover_steps)
    if transport_profile and world_route:
        dx = world_route[-1][0] - target_x
        dz = world_route[-1][1] - target_z
        assert dx * dx + dz * dz <= circle_radius * circle_radius, \
            (world_route[-1], (target_x, target_z), circle_radius)

    p = Phase(width, height)
    native_live = None
    native_placement_results = []
    if native_live_unload:
        tnt_data = cat(hpitool, Path(retail_root), 'maps.hpi',
                       f'Maps/{map_name}.tnt')
        live_map_data = parse_tnt(tnt_data)
        passenger_profile = movement_profile(
            cat(hpitool, Path(retail_root), 'data.hpi', 'gamedata/moveinfo.tdf')
                .decode('latin1'),
            cat(hpitool, Path(retail_root), 'data.hpi',
                f'units/{passenger}.fbi').decode('latin1'))
        native_place = native_placement_oracle(live_map_data, passenger_profile)

        def checked_placement(args, call_number):
            result = native_place(args, call_number)
            native_placement_results.append((args, result))
            return result

        from probe_transport_surface_unload_callbacks import SurfaceUnload
        native_live = SurfaceUnload(placement_result=checked_placement,
            real_mission_removal=True, icd=p.icd, game=GS, freeze_hooks=False)
        unit, mover, type_address = native_live.carrier, native_live.mover, native_live.kind
    else:
        unit = p.unit(sx - fx // 2, sz - fz // 2)
        mover = None
        type_address = TYPE
    assert p.construct() is None
    if mover is None:
        mover = struct.unpack('<I', p.uc.mem_read(unit + 8, 4))[0]
    goal_cell = ((target_x - (fx - 1) * 8) // 16,
                 (target_z - (fz - 1) * 8) // 16)
    if native_live:
        # Start the actual surface-unload handler on the same map-backed carrier
        # that will search, move, arrive and release its passenger below.
        p.uc.mem_write(unit + 0x68, struct.pack('<iii', route_header[1],
                                               sea * 65536, route_header[2]))
        p.uc.mem_write(unit + 0x74, struct.pack('<hh', sx - fx // 2,
                                               sz - fz // 2))
        p.uc.mem_write(unit + 0x78, struct.pack('<hh', fx, fz))
        p.uc.mem_write(type_address + 0x126, struct.pack('<hh', fx, fz))
        p.uc.mem_write(type_address + 0x23e, struct.pack('<H', transport_dist))
        p.uc.mem_write(native_live.mission + 0x22,
                       struct.pack('<i', target_x * 65536))
        p.uc.mem_write(native_live.mission + 0x26, bytes(4))
        p.uc.mem_write(native_live.mission + 0x2a,
                       struct.pack('<i', target_z * 65536))
        native_live.put(unit + 0xc4, native_live.mission + 0x12)
        native_live.put(native_live.passenger + 0xc4, native_live.mission + 0x12)
        p.uc.mem_write(0x64186c, struct.pack('<I', route_tick))
        initial = native_live.dispatch(route_tick)
        assert initial[0:3] == (1, 1, 0x701), initial
        assert native_live.requests == [1], native_live.requests
        controller = native_live.get(native_live.nav + 4)
        assert controller and native_live.get(controller + 4) == native_live.mission
        assert native_live.controller_goal() == (
            0x5f28d8, goal_cell, circle_radius), native_live.controller_goal()
        p.attach_live_request(unit, mover, native_live.nav, controller,
                              (sx - fx // 2, sz - fz // 2), (fx, fz))
    else:
        p.plant_request(unit, (sx - fx // 2, sz - fz // 2), goal_cell)
    if carrier:
        if not native_live:
            p.uc.mem_write(unit + 0x68, struct.pack('<iii', route_header[1],
                                                   sea * 65536, route_header[2]))
    else:
        p.uc.mem_write(unit + 0x68, struct.pack('<iii', start_x * 65536,
                                               sea * 65536, start_z * 65536))
    p.uc.mem_write(unit + 0x78, struct.pack('<hh', fx, fz))
    p.uc.mem_write(unit + 0x7e, struct.pack('<H', heading))
    p.uc.mem_write(mover + 0x36, struct.pack('<H', flags))
    p.uc.mem_write(type_address + 0x126, struct.pack('<hh', fx, fz))
    p.uc.mem_write(type_address + 0x18e, struct.pack('<H', turn))
    p.uc.mem_write(type_address + 0x172, struct.pack('<i', road))
    p.uc.mem_write(type_address + 0x260, struct.pack('<I', 0x80000 if heavy else 0))
    p.uc.mem_write(type_address + 0x16e, struct.pack('<i', water))
    p.uc.mem_write(type_address + 0x192, struct.pack('<hh', max_water, min_water))
    if carrier:
        p.uc.mem_write(type_address + 0x249, bytes([half_cell_ticks]))
        p.uc.mem_write(OBJ + 0x1ad, struct.pack('<I', retry))
    p.uc.mem_write(p.GRID + 4, struct.pack('<hh', fx, fz))

    independent_grade = None
    if native_map_grades:
        if (not carrier or map_name.lower() != 'lake lokken' or
                carrier.lower() != 'vertrans'):
            raise ValueError('--native-map-grades currently checks Lake Lokken Vertrans')
        tnt_data = cat(hpitool, Path(retail_root), 'maps.hpi',
                       f'Maps/{map_name}.tnt')
        map_data = parse_tnt(tnt_data)
        feature_name_pointer = struct.unpack_from('<I', tnt_data, 24)[0]
        feature_count = map_data[5]
        map_feature_names = [
            tnt_data[feature_name_pointer + index * 132 + 4:
                     feature_name_pointer + index * 132 + 132].split(b'\0', 1)[0]
            .decode('latin1') for index in range(feature_count)]
        movement, profile, native_fx, native_fz = native_water_profile(hpitool, retail_root)
        if (native_fx, native_fz) != (fx, fz):
            raise AssertionError(('native map-grade footprint', movement,
                                  (native_fx, native_fz), (fx, fz)))
        independent_grade = native_grade_reader(map_data, profile)

    query_count = 0
    native_grade_queries = set()

    def grade(_uc, args):
        nonlocal query_count
        x, z = struct.unpack('<ii', p.uc.mem_read(args, 8))
        query_count += 1
        if not (0 <= x < width and 0 <= z < height):
            value = 0
        elif independent_grade:
            value = independent_grade(x, z)
            native_grade_queries.add((x, z))
            # 0x508cd0 returns the terrain/cache score. Keep the separately
            # sourced World grade-5 visibility fallback, which is not a terrain
            # score and otherwise makes unexplored cells look traversable.
            if grades[z * width + x] == 5:
                value = 5
        else:
            value = grades[z * width + x]
        return 3, value

    p.icd.hooks[0x4139d0] = grade
    # Let native reconstruction deliver through retail's actual navigator
    # setter. Replacing 0x4e4ea0 with a callback would capture the route but
    # leave the mover without its installed controller/path.
    if not native_live:
        _, error = p.icd.call(0x4e2500,
            (p.HANDLE + 0x1000, target_x * 65536, target_z * 65536, circle_radius), ecx=p.HANDLE)
        assert error is None, error
    native_goal = tuple(struct.unpack('<hh', p.uc.mem_read(p.HANDLE + 8, 4)))
    native_radius, radius_squared = struct.unpack('<ii', p.uc.mem_read(p.HANDLE + 0x0c, 8))
    expected_radius_squared = int(circle_radius * circle_radius / 256 + 0.5)
    assert (native_goal, native_radius, radius_squared) == \
        (goal_cell, circle_radius, expected_radius_squared), \
        (native_goal, goal_cell, native_radius, radius_squared)
    def run_native_search():
        _, error = p.init()
        assert error is None, error
        p.uc.mem_write(OBJ + 0x165, struct.pack('<I', 10_000_000))
        p.uc.mem_write(OBJ + 0x5c, struct.pack('<I', 1))
        _, error = p.step()
        assert error is None, error
        # 415b10 can deliver a direct trace route on this first handoff. Detect
        # the route in the navigator because delivery now runs through retail's
        # actual 0x4e4ea0 setter, not a replacement callback.
        completed = struct.unpack('<I', p.uc.mem_read(p.NAV + 0x10c, 4))[0] != 0
        if not completed:
            assert p.phase() == 2, 'retail reachability tracer rejected the connected map route'
            assert struct.unpack('<I', p.uc.mem_read(OBJ + 4, 4))[0] != 0, \
                'phase-2 handoff did not initialize retail cost-search heap'
            for search_step in range(100_000):
                value, error = p.step()
                assert error is None, (search_step, error)
                if value:
                    completed = True
                    _, error = p.icd.call(0x414450, (0,), ecx=OBJ)
                    assert error is None, error
                    break
        assert completed, completed
        count = struct.unpack('<I', p.uc.mem_read(p.NAV + 0x10c, 4))[0]
        words = struct.unpack('<' + 'h' * (count * 2),
                              p.uc.mem_read(p.NAV + 12, count * 4)) if count else ()
        return list(zip(words[::2], words[1::2]))

    native_route = run_native_search()
    if native_live:
        # The World physical trace intentionally suspends the mission poll so
        # both movers follow the already delivered segment until circle arrival.
        # Keep this exact same controlled deadline in retail; circle arrival
        # still wakes the real unload dispatcher through 0x4e5150.
        native_live.put(native_live.mission + 0x0a,
                        route_tick + native_map_mover_steps + 1000)
    if independent_grade:
        map_features = map_data[4]
        observed_features = set()
        for x, z in native_grade_queries:
            # 0x508cd0 tests the footprint plus its one-cell cached-clearance
            # border through four raw-rectangle probes.
            for qz in range(max(0, z - 1), min(height, z + fz + 1)):
                for qx in range(max(0, x - 1), min(width, x + fx + 1)):
                    feature = map_features[qz * width + qx]
                    if feature < 0xfffa:
                        observed_features.add(feature)
        observed_names = {map_feature_names[index] for index in observed_features
                          if index < len(map_feature_names)}
        assert observed_names <= {'TarWave05'}, (
            'native map-grade route probe encountered an unmodeled feature', observed_names)
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

    map_mover_count = 0
    map_mover_entered_circle = False
    if native_map_mover_steps:
        if not independent_grade or not carrier:
            raise AssertionError('map mover check requires native Lake Lokken Vertrans grades')
        (seed_x, seed_y, seed_z, seed_heading, seed_speed, seed_base, terrain_flags) = world_seed
        _max_velocity, acceleration, braking, unit_turn, waterline = world_type
        if terrain_flags != 0x1000 or seed_speed != 0 or seed_base <= 0:
            raise AssertionError(('World surface mover seed', world_seed, world_type))

        map_heights = map_data[3]
        map_features = map_data[4]
        class_fx, class_fz, max_depth, min_depth, bad_max_depth, bad_min_depth, \
            max_slope, bad_slope, max_water_slope, bad_water_slope = \
            struct.unpack('<6h4B', profile)
        if (class_fx, class_fz) != (fx, fz):
            raise AssertionError(('native mover footprint', (class_fx, class_fz), (fx, fz)))

        def terrain_grade(low, high):
            if low < sea - max_depth or high > sea - min_depth:
                return 0
            slope = high - low
            hard = max_water_slope if low < sea else max_slope
            soft = bad_water_slope if low < sea else bad_slope
            if slope > soft and slope > hard:
                return 0
            if slope > soft:
                return 4
            if low < sea - bad_max_depth or high > sea - bad_min_depth:
                return 4
            return 6

        records = bytearray(width * height * 14)
        raw_grade_words = [0] * (width * ((height + 7) // 8))
        for z in range(height):
            for x in range(width):
                index = z * width + x
                offset = index * 14
                corners = (map_heights[index],
                           map_heights[z * width + min(x + 1, width - 1)],
                           map_heights[min(z + 1, height - 1) * width + x],
                           map_heights[min(z + 1, height - 1) * width + min(x + 1, width - 1)])
                low, high = min(corners), max(corners)
                records[offset + 4] = map_heights[index]
                records[offset + 5] = high
                records[offset + 6] = low
                struct.pack_into('<H', records, offset + 8, map_features[index])
                word = (z >> 3) * width + x
                raw_grade_words[word] |= terrain_grade(low, high) << ((z & 7) * 4)
        p.uc.mem_write(p.cells_addr, bytes(records))
        p.uc.mem_write(p.GMAP, struct.pack('<' + 'I' * len(raw_grade_words),
                                           *raw_grade_words))
        p.uc.mem_write(GS + 0x19ef8, bytes((sea,)))
        feature_count = map_data[5]
        feature_table = p._alloc(max(320, feature_count * 320))
        p.uc.mem_write(feature_table, bytes(max(320, feature_count * 320)))
        p.uc.mem_write(GS + 0x19edc, struct.pack('<I', feature_table))
        p.uc.mem_write(GS + 0x19ec0, struct.pack('<I', feature_count))

        # The native mover maintains its 128px sector index as it advances.
        sector_stride, sector_rows = (width + 7) // 8, (height + 7) // 8
        sector_records = bytearray(sector_stride * sector_rows * 10)
        for sector_z in range(sector_rows):
            for sector_x in range(sector_stride):
                block = [map_heights[z * width + x]
                         for z in range(sector_z * 8, min(height, sector_z * 8 + 8))
                         for x in range(sector_x * 8, min(width, sector_x * 8 + 8))]
                sector_records[(sector_z * sector_stride + sector_x) * 10 + 1] = max(block)
        sector_grid = p._alloc(len(sector_records))
        p.uc.mem_write(sector_grid, bytes(sector_records))
        p.uc.mem_write(GS + 0x19f18, struct.pack('<I', sector_grid))
        p.uc.mem_write(GS + 0x19f1c, struct.pack('<I', sector_stride))
        p.uc.mem_write(GS + 0x600000, struct.pack('<I', GS + 0x700000))
        visibility = struct.unpack('<I', p.uc.mem_read(GS + 0x19ef4, 4))[0]
        p.uc.mem_write(visibility, struct.pack('<' + 'H' * (width * height // 4),
                                               *([0xffff] * (width * height // 4))))
        p.uc.mem_write(GS + 0x19f44, struct.pack('<I', route_tick))
        p.uc.mem_write(0x64186c, struct.pack('<I', route_tick))

        p.uc.mem_write(unit + 0x68, struct.pack('<iii', seed_x, seed_y, seed_z))
        p.uc.mem_write(unit + 0x7e, struct.pack('<H', seed_heading))
        p.uc.mem_write(unit + 0x12b, struct.pack('<i', seed_base))
        p.uc.mem_write(mover + 0x20, struct.pack('<i', seed_speed))
        p.uc.mem_write(mover + 0x30, struct.pack(
            '<I', route_tick + native_map_mover_steps + 1000))
        p.uc.mem_write(mover + 8, bytes(12))
        p.uc.mem_write(mover + 0x14, bytes(12))
        p.uc.mem_write(mover + 0x36, struct.pack('<H', flags | 1))
        p.uc.mem_write(type_address + 0x162, struct.pack('<i', seed_base))
        # Retail stores brakerate before acceleration in the native type block:
        # maxvelocity +0x162, brakerate +0x166, acceleration +0x16a.
        p.uc.mem_write(type_address + 0x166, struct.pack('<i', braking))
        p.uc.mem_write(type_address + 0x16a, struct.pack('<i', acceleration))
        p.uc.mem_write(type_address + 0x126, struct.pack('<hh', fx, fz))
        p.uc.mem_write(type_address + 0x18a, struct.pack('<I', p.GRID))
        p.uc.mem_write(type_address + 0x18e, struct.pack('<H', unit_turn))
        p.uc.mem_write(type_address + 0x23c, bytes((max_slope, max_water_slope)))
        p.uc.mem_write(type_address + 0x24a, b'\x01')
        p.uc.mem_write(type_address + 0x248, bytes((waterline & 0xff,)))
        p.uc.mem_write(p.GRID + 4, struct.pack('<hh', fx, fz))
        p.uc.mem_write(p.GRID + 8, struct.pack('<4h4B', max_depth, min_depth,
            bad_max_depth, bad_min_depth, max_slope, bad_slope,
            max_water_slope, bad_water_slope))
        p.icd.hooks[0x51ad20] = lambda _uc, _args: (1, 0)
        p.icd.hooks[0x56c640] = lambda _uc, _args: (8, 0)

        live_release_step = None
        native_arrival_wakes = 0
        for step, row in enumerate(world_steps, 1):
            p.uc.mem_write(GS + 0x19f44, struct.pack('<I', route_tick + step))
            if native_live:
                stage_before_dispatch = p.uc.mem_read(native_live.mission + 5, 1)[0]
                dispatch_row = native_live.dispatch(route_tick + step)
                if stage_before_dispatch == 1 and dispatch_row[1] == 2:
                    native_arrival_wakes += 1
            else:
                value, error = p.icd.call(0x4d8450, (unit,))
                assert error is None, ('native map mover pre-step', step, error)
            # The surface mission's placement hook is a map-backed Araarch
            # release oracle. Keep retail's own carrier-footprint mover scan
            # active during 0x4dc800 rather than routing it through that hook.
            placement_hooks = p.icd.hooks
            if native_live:
                p.icd.hooks = {address: hook for address, hook in placement_hooks.items()
                               if address != 0x507d10}
            try:
                value, error = p.icd.call(0x4dc800, (unit,), ecx=mover)
            finally:
                p.icd.hooks = placement_hooks
            assert error is None, ('native map mover', step, error)
            value, error = p.icd.call(0x51b2a0, (unit,), ecx=mover)
            assert error is None, ('native map route update', step, error)
            move_flags = struct.unpack('<H', p.uc.mem_read(mover + 0x36, 2))[0]
            native_step = (*struct.unpack('<iii', p.uc.mem_read(unit + 0x68, 12)),
                           struct.unpack('<H', p.uc.mem_read(unit + 0x7e, 2))[0],
                           struct.unpack('<i', p.uc.mem_read(mover + 0x20, 4))[0],
                           (move_flags >> 5) & 7, (move_flags >> 8) & 7,
                           move_flags & 0x1800)
            expected_step = (*row[1:6], *row[6:9])
            assert native_step == expected_step, (
                'native/World real-map mover step', step, native_step,
                expected_step, row[9])
            if native_live and not native_live.get(unit + 0xac):
                live_release_step = step
                break
        map_mover_count = len(world_steps)
        if native_live:
            assert live_release_step is not None, (
                'native surface mission did not release Araarch during the map-backed mover trace',
                {'placement': native_placement_results[-8:],
                 'stage': p.uc.mem_read(native_live.mission + 5, 1)[0],
                 'mission_events': native_live.get(native_live.mission + 0x6A),
                 'unit_events': native_live.get(unit + 0x60),
                 'position': struct.unpack('<iii', p.uc.mem_read(unit + 0x68, 12)),
                 'navigator_controller': native_live.get(native_live.nav + 4),
                 'navigator_path_count': struct.unpack('<I', p.uc.mem_read(
                     native_live.nav + 0x10C, 4))[0],
                 'arrival_wakes': native_arrival_wakes})
            native_live.dispatch(route_tick + live_release_step + 1)
            assert native_live.get(unit + 0x60) == 0, (
                'native surface mission did not retire after its one-tick release tail',
                hex(native_live.get(unit + 0x60)))
            released = struct.unpack('<3i',
                                     p.uc.mem_read(native_live.passenger + 0x68, 12))
            released_origin = (
                int((released[0] / 65536 - (passenger_profile[0] - 1) * 8) // 16),
                int((released[2] / 65536 - (passenger_profile[1] - 1) * 8) // 16))
            assert released_origin == target_cell, (
                'retail unload did not release Araarch at the selected Lake Lokken shore cell',
                released, released_origin, target_cell)
            assert native_placement_results and all(result == 1
                for _, result in native_placement_results), native_placement_results
            assert native_live.get(unit + 0xac) == 0
            assert native_live.get(native_live.passenger + 0xa8) == 0
            assert native_arrival_wakes == 1, \
                ('native navigator arrival did not wake the unload mission exactly once',
                 native_arrival_wakes)
            map_mover_count = live_release_step
            print(f'  Native mission, search, and map mover remained joined through the '
                  f'retail shoreline release at physical step {live_release_step}; '
                  f'navigator arrival advanced the mission exactly once; '
                  f'{len(native_placement_results)} map-backed passenger placement checks passed.')
        final_row = world_steps[map_mover_count - 1]
        final_x, final_z = final_row[1], final_row[3]
        dx = final_x - target_x * 65536
        dz = final_z - target_z * 65536
        map_mover_entered_circle = dx * dx + dz * dz <= (circle_radius * 65536) ** 2
    else:
        map_mover_count = 0

    profile_name = f' {carrier}/{passenger}' if carrier else ''
    arrival = (f', endpoint inside {circle_radius}px unload circle'
               if transport_profile and world_route else '')
    grade_source = (f' native 0x508cd0 map grades on {len(native_grade_queries)} query cells '
                    f'(World grade-5 visibility retained);' if independent_grade else '')
    print(f'PASS: {map_name}{profile_name} unload route matches exactly at World tick '
          f'{route_tick}; {len(world_route)} waypoints{arrival},{grade_source} '
          f'{query_count} native grade queries, {completions} World completion(s), '
          f'{failures} World failure(s)')
    if map_mover_count:
        arrival_note = ('; both finish inside the authored unload circle'
                        if map_mover_entered_circle else
                        '; final position remains outside the unload circle')
        print(f'  Native 0x4dc800 + 0x51b2a0 matches {map_mover_count} World physical '
              f'mover steps over TNT terrain{arrival_note}.')


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
    parser.add_argument('--native-map-grades', action='store_true',
                        help='answer retail route grades with TNT-backed native 0x508cd0')
    parser.add_argument('--native-map-mover-steps', type=int, default=0,
                        help='also compare this many native physical mover ticks on the map (1..2500)')
    parser.add_argument('--native-live-unload', action='store_true',
                        help='keep retail GROUND_UNLOAD active through map-backed movement and passenger release')
    parser.add_argument('--hpitool', default='build/hpitool')
    args = parser.parse_args()
    if bool(args.carrier) != bool(args.passenger):
        parser.error('--carrier and --passenger must be supplied together')
    if not 0 <= args.native_map_mover_steps <= 2500:
        parser.error('--native-map-mover-steps must be 0..2500')
    if args.native_live_unload and args.native_map_mover_steps == 0:
        parser.error('--native-live-unload requires --native-map-mover-steps')
    check_route(args.world_binary, args.retail_root, args.map, tuple(args.start),
                tuple(args.target), args.footprint, args.carrier, args.passenger,
                args.crusades, args.native_map_grades or bool(args.native_map_mover_steps),
                str(Path(args.hpitool).resolve()), args.native_map_mover_steps,
                args.native_live_unload)


if __name__ == '__main__':
    main()
