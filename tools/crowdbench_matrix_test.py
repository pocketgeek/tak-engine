#!/usr/bin/env python3
"""Regression checks for benchmark evidence, censoring, and run identity."""
import importlib.util
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

MODULE_PATH = Path(__file__).with_name("crowdbench_matrix.py")
SPEC = importlib.util.spec_from_file_location("crowdbench_matrix", MODULE_PATH)
matrix = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(matrix)


class MatrixTests(unittest.TestCase):
    def test_variability_and_censored_completion(self):
        rows = [dict(mode="cooperative", seed=7, hash="fixed", tick_ms_mean=value,
                     all_arrived_tick=-1, arrived_settled=4) for value in (1, 2, 3)]
        summary, = matrix.summarize(rows)
        self.assertTrue(summary["outcomes_deterministic"])
        self.assertEqual(summary["metrics"]["tick_ms_mean"]["mean"], 2)
        self.assertEqual(summary["metrics"]["tick_ms_mean"]["stdev"], 1)
        self.assertIsNone(summary["metrics"]["all_arrived_tick"]["mean"])
        self.assertEqual(summary["metrics"]["all_arrived_tick"]["censored"], 3)
        rows[-1]["hash"] = "different"
        self.assertFalse(matrix.summarize(rows)[0]["outcomes_deterministic"])

    def test_seed_and_diagnostic_runs_are_not_pooled(self):
        rows = [dict(mode="retail", seed=seed, latency_observation=enabled, tick_ms_mean=1)
                for seed in (0, 42) for enabled in (False, True)]
        self.assertEqual(len(matrix.summarize(rows)), 4)

    def test_source_provenance_includes_dirty_untracked_code(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            (root / "src").mkdir()
            source = root / "src" / "test.cpp"
            source.write_text("original\n")
            subprocess.run(["git", "-C", str(root), "add", "."], check=True)
            subprocess.run(["git", "-C", str(root), "-c", "user.name=Benchmark Test",
                            "-c", "user.email=benchmark@example.invalid", "commit", "-qm", "fixture"], check=True)
            binary = root / "binary"
            binary.write_bytes(b"frozen executable")
            first = matrix.provenance(binary, root, None)
            source.write_text("modified\n")
            (root / "src" / "new.h").write_text("new input\n")
            second = matrix.provenance(binary, root, None)
            self.assertTrue(second["dirty"])
            self.assertEqual(first["revision"], second["revision"])
            self.assertEqual(first["binary_sha256"], second["binary_sha256"])
            self.assertNotEqual(first["source_tree_sha256"], second["source_tree_sha256"])
            self.assertIn("src/new.h", second["source_files_sha256"])
            self.assertIn("modified", second["tracked_diff"])

    def test_runner_checks_identity_and_writes_all_four_modes(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            binary = root / "fake-crowdbench"
            binary.write_bytes(b"immutable fake benchmark")
            run = subprocess.run
            def fake_run(command, **kwargs):
                if command[0] != str(binary.resolve()):
                    return run(command, **kwargs)
                args = dict(zip(command[1::2], command[2::2]))
                data = {"mode": args["--mode"], "scenario": args["--scenario"], "workers": False,
                        "units_per_player": int(args["--units"]), "players": int(args["--players"]),
                        "moving_percent": int(args["--moving-percent"]), "seed": int(args["--seed"]),
                        "ticks": int(args["--ticks"]), "arrived_settled": 0, "moving_units": 1,
                        "tick_ms_mean": 1, "hash": "deterministic", "all_arrived_tick": -1}
                kwargs["stdout"].write(json.dumps(data) + "\n")
                return subprocess.CompletedProcess(command, 0)
            out = root / "results"
            argv = [str(MODULE_PATH), "--binary", str(binary), "--output", str(out),
                    "--units", "1", "--populations", "1:100", "--ticks", "3",
                    "--rounds", "2", "--seeds", "0", "7"]
            with mock.patch.object(sys, "argv", argv), mock.patch.object(
                    matrix.subprocess, "run", side_effect=fake_run), contextlib.redirect_stdout(io.StringIO()):
                matrix.main()
            manifest = json.loads((out / "manifest.json").read_text())
            self.assertEqual(len(manifest["runs"]), 16)
            self.assertTrue(manifest["binary_unchanged"])
            summaries = json.loads((out / "summary.json").read_text())
            self.assertEqual(len(summaries), 8)
            self.assertEqual({row["mode"] for row in summaries}, set(matrix.MODES))
            self.assertTrue(all(row["runs"] == 2 for row in summaries))


FINAL_PATH = Path(__file__).with_name("navigation_final_matrix.py")
FINAL_SPEC = importlib.util.spec_from_file_location("navigation_final_matrix", FINAL_PATH)
final = importlib.util.module_from_spec(FINAL_SPEC)
FINAL_SPEC.loader.exec_module(final)


class FinalMatrixTests(unittest.TestCase):
    def fake(self, binaries, calls, broken=None):
        real = subprocess.run
        def run(command, **kwargs):
            if command[0] == "taskset":
                command = command[3:]
            if command[0] not in binaries:
                return real(command, **kwargs)  # provenance/hardware probes
            calls.append(command)
            args = dict(zip(command[1::2], command[2::2]))
            data = {"mode": args["--mode"], "scenario": args["--scenario"], "workers": False,
                    "units_per_player": int(args["--units"]), "players": int(args["--players"]),
                    "moving_percent": int(args["--moving-percent"]), "seed": int(args["--seed"]),
                    "ticks": int(args["--ticks"]), "arrived_settled": 1, "moving_units": 2,
                    "crossed_middle": 2, "tick_ms_mean": 1.0 + len(calls) % 3, "tick_ms_p95": 2.0,
                    "tick_ms_p99": 3.0, "hash": "same", "all_arrived_tick": -1,
                    "route_lifecycle_balanced": True, "build_ndebug": True, "build_optimized": True,
                    "route_request_to_delivery_ticks_received_only_p95": 5}
            if broken and broken in command:
                data["build_optimized"] = False
            return subprocess.CompletedProcess(command, 0, json.dumps(data) + "\n", "")
        return run

    def invoke(self, argv, binaries, calls, broken=None):
        with mock.patch.object(sys, "argv", [str(FINAL_PATH)] + argv), mock.patch.object(
                final.subprocess, "run", side_effect=self.fake(binaries, calls, broken)), \
                contextlib.redirect_stdout(io.StringIO()):
            final.main()

    def test_timing_alternates_pairs_and_reports_both_roles(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            a, b = root / "a", root / "b"
            a.write_bytes(b"baseline")
            b.write_bytes(b"candidate")
            calls = []
            out = root / "timing"
            self.invoke(["run", "--phase", "timing", "--binary", f"baseline={a}", "--binary", f"candidate={b}",
                         "--cpus", "0", "--scenarios", "open", "doors", "--populations", "10:1:100",
                         "--modes", "retail", "--seeds", "0", "--ticks", "5", "--output", str(out)],
                        {str(a.resolve()), str(b.resolve())}, calls)
            self.assertEqual(len(calls), 12)
            self.assertFalse(any("--latency" in c for c in calls))
            # Adjacent runs are one case's pair, and the leading binary alternates.
            firsts = [calls[i][0] for i in range(0, len(calls), 2)]
            self.assertTrue(all(calls[i][1:] == calls[i + 1][1:] for i in range(0, len(calls), 2)))
            self.assertIn(str(a.resolve()), firsts)
            self.assertIn(str(b.resolve()), firsts)
            summary = (out / "summary.md").read_text()
            self.assertIn("candidate/baseline median", summary)
            self.assertIn("Repeated-run deterministic outcome mismatches: **0**", summary)
            self.assertTrue((out / "results.csv").read_text().startswith("phase,role,mode"))

    def test_outcome_uses_diagnostics_and_rejects_unoptimized_binary(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            a = root / "a"
            a.write_bytes(b"one")
            calls = []
            with self.assertRaises(SystemExit):
                self.invoke(["run", "--phase", "outcome", "--binary", f"checkpoint={a}", "--cpus", "0-1",
                             "--scenarios", "doors", "maze", "--populations", "10:1:100", "--modes", "cooperative",
                             "--seeds", "0", "7", "--ticks", "5", "--long-ticks", "9", "--long-population",
                             "20:1:100", "--output", str(root / "outcome")],
                            {str(a.resolve())}, calls, broken="maze")
            self.assertEqual(len(calls), 8)  # 2 scenarios x 2 seeds, plus their long cases
            self.assertTrue(all("--latency" in c and "--allocations" in c for c in calls))
            manifest = json.loads((root / "outcome" / "manifest.json").read_text())
            self.assertEqual(len(manifest["errors"]), 4)
            self.assertIn("not an optimized", manifest["errors"][0]["error"])


if __name__ == "__main__":
    unittest.main()
