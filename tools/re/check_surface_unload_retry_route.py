#!/usr/bin/env python3
"""Pair the native same-mission sea retry with its physically followed route.

The dispatcher probe retries the original unload from a remote point on the
fixture's connected water lane and verifies the replacement exact-site circle.
The route probe then runs the matching World geometry from that same point and
landing site, feeding World-produced grades to retail's real search and mover.
The dispatch and physical traces are separate emulator instances, but their
position and destination are asserted equal at the handoff.
"""
import argparse

from check_surface_unload_route import check_variant
from probe_transport_surface_unload_retry_cancel import check_same_trip_out_of_range_retry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('world_binary', nargs='?', default='build-o2/transport_test')
    parser.add_argument('--steps', type=int, default=1000,
                        help='paired physical mover steps after the remote retry position (1..1000)')
    args = parser.parse_args()
    if not 1 <= args.steps <= 1000:
        parser.error('--steps must be between 1 and 1000')

    retry = check_same_trip_out_of_range_retry()
    route = check_variant(args.world_binary, 8, args.steps)
    assert retry['position'][0] == route['seed'][0], (retry['position'], route['seed'])
    assert retry['position'][2] == route['seed'][2], (retry['position'], route['seed'])
    assert (retry['destination'][0], retry['destination'][2]) == route['target'], (
        retry['destination'], route['target'])
    assert retry['controller_goal'][2] == route['circle_radius'] == 116, (
        retry['controller_goal'], route['circle_radius'])
    print(f"PASS: the native replacement controller's remote position and original landing "
          f"site match the {route['physical_steps']}-step native/World route and mover trace; "
          f"route={route['native_route']}")


if __name__ == '__main__':
    main()
