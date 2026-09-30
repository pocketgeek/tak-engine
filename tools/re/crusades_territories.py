#!/usr/bin/env python3
"""Audit a user-supplied Darien.def; emit aggregate metadata, never its prose.

This deliberately supports the observed tagged format, not arbitrary TDF or
all possible Boneyards messages. It does not infer borders or ownership.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

RECORD = re.compile(r'\{\[DarienMap(.*?)\]\}', re.DOTALL)
FIELD = re.compile(r'<([SI]):([a-z_]+)=([^<>]*)>', re.DOTALL)
WORLD = {'command': str, 'name': str, 'height': int, 'width': int,
         'parcels': int, 'borders': int}
PARCEL = {'command': str, 'name': str, 'chatareaid': int, 'description': str,
          'xfireanchor': int, 'yfireanchor': int, 'xtextanchor': int,
          'ytextanchor': int, 'nativerace': str, 'nativeicon': int, 'terrain': str}


def parse_definition(data):
    text = data.decode('cp1252')
    records = []
    end = 0
    for record in RECORD.finditer(text):
        if text[end:record.start()].strip():
            raise ValueError('Unrecognized content between records')
        body = record.group(1)
        fields = {}
        pos = 0
        for field in FIELD.finditer(body):
            if body[pos:field.start()].strip():
                raise ValueError('Malformed field')
            kind, key, value = field.groups()
            if key in fields:
                raise ValueError(f'Duplicate field: {key}')
            if kind == 'I':
                if not re.fullmatch(r'-?[0-9]+', value):
                    raise ValueError(f'Invalid integer: {key}')
                value = int(value)
            fields[key] = value
            pos = field.end()
        if body[pos:].strip():
            raise ValueError('Malformed trailing field')
        schema = {'world_attribs': WORLD, 'parcel': PARCEL}.get(fields.get('command'))
        if schema is None or set(fields) != set(schema):
            raise ValueError('Unknown command or unexpected field set')
        if any(type(fields[key]) is not expected for key, expected in schema.items()):
            raise ValueError('Field type mismatch')
        records.append(fields)
        end = record.end()
    if text[end:].strip() or not records:
        raise ValueError('Missing records or unrecognized trailing content')
    if records[0]['command'] != 'world_attribs' or any(
            r['command'] != 'parcel' for r in records[1:]):
        raise ValueError('Expected one world header followed by parcels')
    world, parcels = records[0], records[1:]
    if min(world[k] for k in ('width', 'height', 'parcels')) <= 0 or world['borders'] < 0:
        raise ValueError('Invalid world dimensions/counts')
    if world['parcels'] != len(parcels):
        raise ValueError('Declared parcel count differs from observed count')
    for key in ('name', 'chatareaid'):
        if len({p[key] for p in parcels}) != len(parcels):
            raise ValueError(f'Duplicate parcel {key}')
    return world, parcels


def summarize(data):
    world, parcels = parse_definition(data)
    return {
        'schema_version': 1,
        'source_sha256': hashlib.sha256(data).hexdigest(),
        'source_bytes': len(data),
        'encoding': 'cp1252',
        'declared': {k: world[k] for k in ('width', 'height', 'parcels', 'borders')},
        'observed_parcels': len(parcels),
        'unique_names': len({p['name'] for p in parcels}),
        'unique_chatareaids': len({p['chatareaid'] for p in parcels}),
        'native_race_counts': dict(sorted(Counter(p['nativerace'] for p in parcels).items())),
        'terrain_counts': dict(sorted(Counter(p['terrain'] for p in parcels).items())),
        'numeric_ranges': {k: [min(p[k] for p in parcels), max(p[k] for p in parcels)]
                           for k in PARCEL if PARCEL[k] is int},
        'multiline_descriptions': sum('\n' in p['description'] for p in parcels),
        'border_edge_records': 0,
        'limitations': ['Declared border count is not a recovered adjacency graph.',
                        'No ownership or campaign rules inferred.',
                        'Header dimensions preserved verbatim; axes not corrected.'],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('definition', type=Path)
    args = parser.parse_args()
    try:
        result = summarize(args.definition.read_bytes())
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == '__main__':
    main()
