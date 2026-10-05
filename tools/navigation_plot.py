#!/usr/bin/env python3
"""Plot observed crowdbench positions, never planned paths, for visual review."""
import argparse
import csv
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("traces", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--ticks", nargs="+", type=int, default=[0, 1500, 3000, 6000])
    args = parser.parse_args()
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import Rectangle

    figure, axes = plt.subplots(len(args.traces), len(args.ticks), squeeze=False,
                                figsize=(4 * len(args.ticks), 3.4 * len(args.traces)))
    for row, path in enumerate(args.traces):
        snapshots = {tick: {} for tick in args.ticks}
        trajectories = {}
        with path.open(newline="") as handle:
            for sample in csv.DictReader(handle):
                tick, unit = int(sample["tick"]), int(sample["id"])
                x, z = int(sample["x_raw"]) / 65536, int(sample["z_raw"]) / 65536
                if unit <= 12:
                    trajectories.setdefault(unit, []).append((tick, x, z))
                for limit in args.ticks:
                    if tick <= limit:
                        snapshots[limit][unit] = (x, z, int(sample["orders"]),
                                                  int(sample["foot_x"]) * 16,
                                                  int(sample["foot_z"]) * 16)
        for column, limit in enumerate(args.ticks):
            ax = axes[row][column]
            for unit, samples in trajectories.items():
                points = [(x, z) for tick, x, z in samples if max(0, limit - 600) <= tick <= limit]
                if points:
                    ax.plot(*zip(*points), linewidth=.6, alpha=.6)
            for x, z, orders, width, height in snapshots[limit].values():
                ax.add_patch(Rectangle((x - width / 2, z - height / 2), width, height,
                                      color="#286baf" if orders else "#da7725", alpha=.6))
            ax.autoscale_view()
            ax.set_aspect("equal", adjustable="datalim")
            ax.set_title(f"{path.name.replace('.trace.csv.units.csv', '')}\ntick {limit}", fontsize=8)
            ax.set_xlabel("observed x (pixels)")
            ax.set_ylabel("observed z (pixels)")
    figure.suptitle("Observed footprints; blue: active orders, orange: retired orders\n"
                   "Lines: last 600 ticks of the first 12 units. Retirement alone is not legal arrival.", fontsize=11)
    figure.tight_layout(rect=(0, 0, 1, .95))
    figure.savefig(args.output, dpi=160)


if __name__ == "__main__":
    main()
