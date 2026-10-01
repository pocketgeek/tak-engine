#!/usr/bin/env python3
"""Run offline admin CLI checks against synthetic temporary SQLite databases.

No retail data, accounts server, or network is needed. Fixtures use the store's
documented snapshot/battle encodings so backup comparison covers actual rows.
"""
import argparse
import contextlib
import json
import os
from pathlib import Path
import shutil
import sqlite3
import struct
import subprocess
import tempfile
import time


def string(value):
    data = value.encode('utf-8')
    return struct.pack('<I', len(data)) + data


def contents(path):
    """Compare every persistent application row, including opaque BLOB bytes."""
    with sqlite3.connect(path) as db:
        names = [row[0] for row in db.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name")]
        return {name: sorted(db.execute('SELECT * FROM "' + name + '"').fetchall(), key=repr)
                for name in names}


@contextlib.contextmanager
def service_lock(path):
    """Hold the same OS lock as CampaignServiceLease, in a separate process."""
    companion = Path(str(path.resolve()) + '.service-lock')
    if os.name == 'nt':
        import ctypes
        from ctypes import wintypes

        class Overlapped(ctypes.Structure):
            _fields_ = [('Internal', ctypes.c_size_t), ('InternalHigh', ctypes.c_size_t),
                        ('Offset', wintypes.DWORD), ('OffsetHigh', wintypes.DWORD),
                        ('hEvent', wintypes.HANDLE)]

        api = ctypes.WinDLL('kernel32', use_last_error=True)
        api.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                   ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        api.CreateFileW.restype = wintypes.HANDLE
        api.LockFileEx.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD,
                                  wintypes.DWORD, wintypes.DWORD, ctypes.POINTER(Overlapped)]
        api.LockFileEx.restype = wintypes.BOOL
        api.CloseHandle.argtypes = [wintypes.HANDLE]
        api.CloseHandle.restype = wintypes.BOOL
        handle = api.CreateFileW(str(companion), 0xc0000000, 3, None, 4, 0x80, None)
        assert handle != wintypes.HANDLE(-1).value, ctypes.get_last_error()
        offset = Overlapped()
        try:
            assert api.LockFileEx(handle, 3, 0, 1, 0, ctypes.byref(offset)), ctypes.get_last_error()
            yield
        finally:
            api.CloseHandle(handle)
    else:
        import fcntl
        with companion.open('a+b') as handle:
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            yield


def seed_battles(path):
    """Valid issued, started, completed and expired offers with secret sentinels."""
    now = int(time.time()) - 10
    context = b'TAKCB1' + string('maps/authored.ota') + string('map-digest') + string('rules-digest')
    context += string('alice') + struct.pack('<BQ', 1, 0)
    context += string('bravo') + struct.pack('<BQ', 2, 0)
    with sqlite3.connect(path) as db:
        for account, alliance in [('alice', 1), ('bravo', 2)]:
            db.execute('INSERT INTO campaign_participants VALUES(?,?,?)', ('synthetic', account, 0))
            db.execute('INSERT INTO allegiance_events VALUES(?,?,?,?,?,?)',
                       ('synthetic', account, 0, alliance, now, now))
        for suffix, state in [('issued', 0), ('started', 1), ('completed', 3), ('expired', 4)]:
            battle_id = 'issued:' + suffix
            lifecycle = [0] if state == 0 else [0, 1, 3] if state == 3 else [0, state]
            db.execute('INSERT INTO issued_battles VALUES(?,?,?,?,?,?,?,?,?)',
                       (battle_id, 'synthetic', 1, 1, context, now, now + 3600,
                        'DO-NOT-PRINT-LAUNCH-' + suffix, len(lifecycle) - 1))
            db.execute('INSERT INTO issued_battle_rules VALUES(?,?)', (battle_id, 'historical-darien-v1'))
            for revision, step in enumerate(lifecycle):
                db.execute('INSERT INTO battle_status_events VALUES(?,?,?,?)', (battle_id, revision, step, now))
            for account in ['alice', 'bravo']:
                db.execute('INSERT INTO battle_participants VALUES(?,?,?,?)',
                           (battle_id, 'synthetic', account, now))
            if state in [1, 3]:
                db.execute('INSERT INTO battle_rooms VALUES(?,?)', ('DO-NOT-PRINT-ROOM-' + suffix, battle_id))
            if state == 3:
                replay_id, digest = 'synthetic-completed-replay', 'a' * 64
                result = b'TAKCR1' + struct.pack('<BB', 0, 1) + string('alice')
                result += struct.pack('<QQQ', 20, 123, 456)
                result += string('synthetic-admin-test') + string(replay_id) + string(digest) + b'\x02'
                for account, faction, team, defeated in [('alice', 'aramon', 1, False),
                                                       ('bravo', 'taros', 2, True)]:
                    result += string(account) + struct.pack('<qqqqq', 2, 1, -3, 4, 5)
                    result += string(faction) + struct.pack('<qB', team, defeated)
                db.execute('INSERT INTO verified_match_results VALUES(?,?,?,?,?,?,?)',
                           (battle_id, replay_id, digest, 0, 'alice', result, now))
                db.execute('INSERT INTO territory_battle_history VALUES(?,?,?,?)',
                           ('synthetic', 1, battle_id, now))
                snapshot = db.execute('SELECT snapshot FROM campaign_events WHERE campaign_id=? AND revision=?',
                                      ('synthetic', 1)).fetchone()[0]
                db.execute('INSERT INTO rule_decisions VALUES(?,?,?,?,?,?,?,?)',
                           (battle_id, 'historical-darien-v1', 1, 1, 0, 0, 'Historical rules remain unknown', snapshot))


def test(binary):
    checks = 0

    def run(database, *args, ok=True):
        nonlocal checks
        result = subprocess.run([str(binary), str(database), *map(str, args)],
                                text=True, encoding='utf-8', capture_output=True, timeout=15)
        assert (result.returncode == 0) == ok, (args, result.returncode, result.stdout, result.stderr)
        assert 'DO-NOT-PRINT-' not in result.stdout + result.stderr, (args, 'capability leaked')
        if not ok:
            assert result.stderr, (args, 'missing actionable error')
        checks += 1
        return [json.loads(line) for line in result.stdout.splitlines()]

    with tempfile.TemporaryDirectory(prefix='tak-admin-') as temporary:
        root = Path(temporary) / 'Café-世界'
        root.mkdir()
        database = root / 'campaign-世界.sqlite'
        definition = root / 'synthetic-Café.campaign'
        definition.write_text('campaign 1 "synthetic" "Synthetic 世界"\n'
                              'territory 1 "Coast Café"\nmap 1 "maps/authored.ota"\n'
                              'native 1 "aramon"\nterritory 2 "Unknown"\n', encoding='utf-8')
        backup = root / 'backup-世界.sqlite'

        # Missing reads/backup never create an empty DB (or companion file).
        for args in [('health',), ('status',), ('campaigns',), ('backup', backup)]:
            run(database, *args, ok=False)
            assert not database.exists() and not backup.exists()
            assert not Path(str(database) + '.service-lock').exists()
        for args in [('start', definition),
                     ('start', definition, '--actor', 'Alice', '--reason', 'bad identity'),
                     ('start', definition, '--actor', 'admin', '--reason', '   '),
                     ('start', definition, '--actor', 'admin', '--reason', 'x' * 513),
                     ('start', definition, '--actor', 'admin', '--reason', 'test', '--force'),
                     ('start', definition, '--actor', 'admin', '--actor', 'admin', '--reason', 'test')]:
            run(database, *args, ok=False)
            assert not database.exists()

        # A campaign must be representable by the shipped server/client wire
        # format before start is allowed to create a database or service lock.
        invalid_definitions = {
            'id-129': 'campaign 1 "' + 'a' * 129 + '" "Oversized ID"\nterritory 1 "One"\n',
            'neighbors-257': 'campaign 1 "too-many-neighbors" "Large star"\n' +
                ''.join(f'territory {n} "Territory {n}"\n' for n in range(1, 259)) +
                'neighbors 1 ' + ' '.join(map(str, range(2, 259))) + '\n' +
                ''.join(f'neighbors {n} 1\n' for n in range(2, 259)),
            'territories-1025': 'campaign 1 "too-many-territories" "Large state"\n' +
                ''.join(f'territory {n} "Territory {n}"\n' for n in range(1, 1026)),
            'payload-oversize': 'campaign 1 "oversized-payload" "Large payload"\n' +
                ''.join(f'territory {n} "' + 'a' * 1024 + f'"\nnative {n} "' + 'b' * 1024 +
                        f'"\nterrain {n} "' + 'c' * 1024 + '"\n' for n in range(1, 83)),
            'invalid-utf8': b'campaign 1 "bad-utf8" "Invalid \xff"\nterritory 1 "One"\n',
        }
        for name, text in invalid_definitions.items():
            invalid = root / (name + '.campaign')
            invalid.write_bytes(text.encode('utf-8') if isinstance(text, str) else text)
            run(database, 'start', invalid, '--actor', 'admin', '--reason', 'Reject unservable definition', ok=False)
            assert not database.exists()
            assert not Path(str(database) + '.service-lock').exists()

        run(database, 'start', definition, '--actor', 'admin', '--reason', 'Authored campaign start')
        first = run(database, 'campaign', 'synthetic', '--limit', 1)
        assert first[0]['revision'] == 0 and first[0]['policy'] == 'historical-darien-v1'
        assert first[1]['name'] == 'Coast Café' and first[1]['owner'] is None
        assert first[1]['assigned_map'] is None and first[1]['honor']['battle'] is None
        assert first[-1] == {'kind': 'page', 'truncated': True, 'next_after': 1}
        second = run(database, 'campaign', 'synthetic', '--after', 1, '--limit', 1)
        assert second[1]['id'] == 2 and not second[-1]['truncated']
        assert run(database, 'campaigns', '--limit', 1)[0]['id'] == 'synthetic'
        initial_contents = contents(database)
        run(database, 'start', definition, '--actor', 'admin', '--reason', 'Duplicate', ok=False)
        assert contents(database) == initial_contents
        report = run(database, 'health')[0]
        assert report['healthy'] and report['complete'] and report['campaigns'] == 1

        # Conflict and missing attribution leave both audit/history unchanged.
        for args in [('reset', 'synthetic', '--expected-revision', 7, '--actor', 'admin', '--reason', 'stale'),
                     ('reset', 'synthetic', '--expected-revision', 0, '--reason', 'no actor'),
                     ('reset', 'synthetic', '--expected-revision', '00', '--actor', 'admin', '--reason', 'noncanonical'),
                     ('reset', 'synthetic', '--expected-revision', 0, '--actor', 'admin', '--reason', '')]:
            run(database, *args, ok=False)
            assert contents(database) == initial_contents
        run(database, 'reset', 'synthetic', '--expected-revision', 0, '--actor', 'operator', '--reason', 'Reset unknown state')
        events = run(database, 'events', 'synthetic', '--limit', 1)
        assert events[0]['revision'] == 0 and events[-1]['truncated']
        after = run(database, 'events', 'synthetic', '--after', 0, '--limit', 1)
        assert after[0]['revision'] == 1 and not after[-1]['truncated']
        audit = run(database, 'audit', 'synthetic', '--limit', 1)
        assert audit[0]['actor'] == 'admin' and audit[-1]['truncated']
        after = run(database, 'audit', 'synthetic', '--after', audit[0]['sequence'], '--limit', 1)
        assert after[0]['actor'] == 'operator' and after[0]['reason'] == 'Reset unknown state'

        # Seed real binary contexts and preserve both launch and room secrets.
        seed_battles(database)
        for suffix in ['issued', 'started', 'completed', 'expired']:
            inspected = run(database, 'battle', 'issued:' + suffix)[0]
            assert inspected['state'] == suffix and inspected['participants'] == ['alice', 'bravo']
        before = contents(database)
        run(database, 'reset', 'synthetic', '--expected-revision', 1,
            '--actor', 'admin', '--reason', 'Pending battle reset', ok=False)
        assert contents(database) == before
        for suffix, expected, revision in [('issued', 'started', 1), ('started', 'issued', 1),
                                           ('issued', 'issued', 0), ('completed', 'started', 1),
                                           ('expired', 'issued', 1)]:
            run(database, 'cancel-battle', 'issued:' + suffix, '--expected-state', expected,
                '--expected-revision', revision, '--actor', 'admin', '--reason', 'Rejected cancel', ok=False)
            assert contents(database) == before
        for suffix in ['issued', 'started']:
            cancelled = run(database, 'cancel-battle', 'issued:' + suffix, '--expected-state', suffix,
                            '--expected-revision', 1, '--actor', 'admin', '--reason', 'Offline cancellation')[0]
            assert cancelled['state'] == 'cancelled'
            cancelled_contents = contents(database)
            run(database, 'cancel-battle', 'issued:' + suffix, '--expected-state', suffix,
                '--expected-revision', 1, '--actor', 'admin', '--reason', 'Repeated cancellation', ok=False)
            assert contents(database) == cancelled_contents
        audit = run(database, 'audit', 'synthetic')
        rows = [row for row in audit if row['kind'] == 'admin_event']
        assert len(rows) == 4
        assert rows[-1]['before_state'] == 'started' and rows[-1]['after_state'] == 'cancelled'
        assert rows[-1]['before_revision'] == rows[-1]['after_revision'] == 1
        # Audit append-only triggers reject alteration/deletion.
        with sqlite3.connect(database) as db:
            for sql in ['UPDATE admin_events SET reason=\'tampered\'',
                        'DELETE FROM admin_events']:
                try:
                    db.execute(sql)
                except sqlite3.DatabaseError:
                    pass
                else:
                    raise AssertionError('audit was mutable: ' + sql)
        assert contents(database) == cancelled_contents

        # Every command takes the lease, even status and backup; canonical
        # spelling with '..' must contend with the same existing process lock.
        with service_lock(database):
            for args in [('health',), ('status',), ('campaigns',), ('campaign', 'synthetic'),
                         ('events', 'synthetic'), ('audit', 'synthetic'), ('battle', 'issued:started'),
                         ('backup', backup), ('start', definition, '--actor', 'admin', '--reason', 'locked'),
                         ('reset', 'synthetic', '--expected-revision', 1, '--actor', 'admin', '--reason', 'locked'),
                         ('cancel-battle', 'issued:issued', '--expected-state', 'issued',
                          '--expected-revision', 1, '--actor', 'admin', '--reason', 'locked')]:
                run(database, *args, ok=False)
            run(root / '..' / root.name / database.name, 'health', ok=False)
            assert not backup.exists()
        assert run(database, 'status')[0]['healthy']
        assert not run(database, 'health', '--limit', 1, ok=False)[0]['complete']

        # Backup refuses overwrite/aliases and preserves all rows in a restored
        # independent file. Then exercise restored store, not only SQLite count.
        run(database, 'backup', database, ok=False)
        run(database, 'backup', root / '..' / root.name / database.name, ok=False)
        for suffix in ['-journal', '-wal', '-shm', '.service-lock']:
            destination = Path(str(database) + suffix)
            before_bytes = destination.read_bytes() if destination.exists() else None
            run(database, 'backup', destination, ok=False)
            run(database, 'backup', root / '..' / root.name / destination.name, ok=False)
            if before_bytes is None:
                assert not destination.exists(), destination
            else:
                assert destination.read_bytes() == before_bytes
            if os.name == 'nt':
                run(database, 'backup', Path(str(database) + suffix.upper()), ok=False)
        assert contents(database) == cancelled_contents
        existing = root / 'existing.sqlite'
        existing.write_bytes(b'preserve this file')
        run(database, 'backup', existing, ok=False)
        assert existing.read_bytes() == b'preserve this file'
        for suffix in ['-journal', '-wal', '-shm', '.service-lock']:
            unused = root / ('unused' + suffix + '.sqlite')
            companion = Path(str(unused) + suffix)
            companion.write_bytes(b'preserve existing destination companion')
            run(database, 'backup', unused, ok=False)
            assert not unused.exists()
            assert companion.read_bytes() == b'preserve existing destination companion'
        if os.name != 'nt':
            symlink = root / 'symlink.sqlite'
            symlink.symlink_to(database)
            run(database, 'backup', symlink, ok=False)
            with service_lock(database):
                run(symlink, 'health', ok=False)
            symlink.unlink()
        hardlink = root / 'hardlink.sqlite'
        os.link(database, hardlink)
        try:
            run(database, 'backup', hardlink, ok=False)
            run(hardlink, 'health', ok=False)
        finally:
            hardlink.unlink()
        run(database, 'backup', backup)
        original_contents = contents(database)
        assert contents(backup) == original_contents
        run(database, 'backup', backup, ok=False)
        restored = root / 'restored-Café.sqlite'
        shutil.copyfile(backup, restored)
        assert run(restored, 'health')[0]['healthy']
        assert run(restored, 'campaign', 'synthetic')[0]['revision'] == 1
        assert run(restored, 'battle', 'issued:started')[0]['state'] == 'cancelled'
        assert contents(restored) == original_contents
        restored_audit = run(restored, 'audit', 'synthetic')
        assert restored_audit == audit
        run(restored, 'reset', 'synthetic', '--expected-revision', 1,
            '--actor', 'operator', '--reason', 'Restored campaign operation')
        assert run(restored, 'campaign', 'synthetic')[0]['revision'] == 2
        assert contents(database) == original_contents and contents(backup) == original_contents

        # A semantic validation failure after SQLite has copied the target must
        # remove the incomplete backup, while retaining the source untouched.
        corrupt = root / 'corrupt-source.sqlite'
        shutil.copyfile(backup, corrupt)
        with sqlite3.connect(corrupt) as db:
            trigger = db.execute("SELECT sql FROM sqlite_master WHERE name='campaign_definition_no_update'").fetchone()[0]
            db.execute('DROP TRIGGER campaign_definition_no_update')
            db.execute("UPDATE campaigns SET definition='invalid synthetic definition'")
            db.execute(trigger)
        corrupt_contents = contents(corrupt)
        incomplete = root / 'incomplete-backup.sqlite'
        run(corrupt, 'backup', incomplete, ok=False)
        assert contents(corrupt) == corrupt_contents
        assert not incomplete.exists()
        for suffix in ['-journal', '-wal', '-shm', '.service-lock']:
            assert not Path(str(incomplete) + suffix).exists()

        # Rejected numeric arguments must not silently migrate an older schema
        # or even create a companion lock for the otherwise valid database.
        old_schema = root / 'version-eight.sqlite'
        shutil.copyfile(backup, old_schema)
        with sqlite3.connect(old_schema) as db:
            db.execute('DROP TABLE admin_events')
            db.execute('PRAGMA user_version=8')
        old_contents = contents(old_schema)
        for args in [('health', '--limit', 0), ('events', 'synthetic', '--after', '-2'),
                     ('campaign', 'synthetic', '--after', '4294967296'), ('audit', 'synthetic', '--limit', 65)]:
            run(old_schema, *args, ok=False)
            with sqlite3.connect(old_schema) as db:
                assert db.execute('PRAGMA user_version').fetchone()[0] == 8
            assert contents(old_schema) == old_contents
            assert not Path(str(old_schema) + '.service-lock').exists()

        # Malformed options/counts and bounded cursor values fail closed.
        for args in [('health', 'extra'), ('campaigns', '--limit', 65), ('campaigns', '--limit', 0),
                     ('campaigns', '--limit', '01'), ('events', 'synthetic', '--after', -2),
                     ('campaign', 'synthetic', '--after', 4294967296), ('audit', 'synthetic', '--after', -1),
                     ('campaigns', '--limit', 2, '--limit', 3), ('backup', backup, '--force'),
                     ('battle', 'issued:started', 'extra'), ('unknown-command',),
                     ('reset', 'synthetic', '--expected-revision', 1, '--actor', 'admin', '--reason', 'x', '--force')]:
            run(database, *args, ok=False)
            assert contents(database) == original_contents
        run(':memory:', 'health', ok=False)
        bad_schema = root / 'unsupported.sqlite'
        shutil.copyfile(backup, bad_schema)
        with sqlite3.connect(bad_schema) as db:
            db.execute('PRAGMA user_version=2147483647')
        run(bad_schema, 'health', ok=False)
        with sqlite3.connect(bad_schema) as db:
            assert db.execute('PRAGMA user_version').fetchone()[0] == 2147483647

        # Legacy event/audit text may contain invalid UTF-8 even though the
        # definition/state itself is valid. Inspecting it must still produce
        # valid JSON, retain good Unicode, and never rewrite the stored bytes.
        legacy = root / 'legacy-text.sqlite'
        shutil.copyfile(backup, legacy)
        invalid_bytes = (b'\x80\xc0\xaf\xe0\x80\x80\xed\xa0\x80\xf4\x90\x80\x80\xff\xc2')
        legacy_reason = 'Café 世界 🍃: '.encode('utf-8') + invalid_bytes
        expected_reason = 'Café 世界 🍃: ' + '\ufffd' * len(invalid_bytes)
        with sqlite3.connect(legacy) as db:
            for table, trigger_name, where in [('campaign_events', 'campaign_events_no_update', 'revision=0'),
                                               ('admin_events', 'admin_events_no_update', 'sequence=1')]:
                trigger = db.execute('SELECT sql FROM sqlite_master WHERE name=?', (trigger_name,)).fetchone()[0]
                db.execute('DROP TRIGGER ' + trigger_name)
                db.execute('UPDATE ' + table + ' SET reason=CAST(? AS TEXT) WHERE ' + where, (legacy_reason,))
                db.execute(trigger)
        assert run(legacy, 'campaign', 'synthetic')[0]['name'] == 'Synthetic 世界'
        assert run(legacy, 'events', 'synthetic')[0]['reason'] == expected_reason
        assert run(legacy, 'audit', 'synthetic')[0]['reason'] == expected_reason
        with sqlite3.connect(legacy) as db:
            assert db.execute('SELECT CAST(reason AS BLOB) FROM campaign_events WHERE revision=0').fetchone()[0] == legacy_reason
            assert db.execute('SELECT CAST(reason AS BLOB) FROM admin_events WHERE sequence=1').fetchone()[0] == legacy_reason
        if os.name != 'nt':
            # POSIX argv can carry non-UTF-8 bytes; Windows utf8Main already
            # rejects unpaired wide surrogates before entering the command.
            run(legacy, 'reset', 'synthetic', '--expected-revision', 1,
                '--actor', 'operator', '--reason', 'POSIX invalid byte: \udcff')
            audit = run(legacy, 'audit', 'synthetic')
            assert audit[-2]['reason'] == 'POSIX invalid byte: \ufffd'
    print(f'PASS: {checks} offline Crusades admin CLI checks')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path, help='built crusades_admin executable')
    args = parser.parse_args()
    binary = args.binary.resolve()
    assert binary.is_file(), binary
    test(binary)


if __name__ == '__main__':
    main()
