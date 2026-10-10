#!/usr/bin/env bash
# we_timing.sh --base BUILD --cand BUILD [options]   -- the fast WE A/B timing round (docs/development.md "WE timing")
#
# Replaces "replay R-2h / L-2h from tick 0, twice, swapped" (about 70 min) with swapped concurrent pairs that start
# from harvested situations (tools/scenarios/we-timing/*.scn.gz, cut by tools/we_timing_harvest.sh) just before the
# measured window and simulate only warm-up + window. A round is about 15 minutes. It is TIMING only: identity (hashes,
# replays, crowdbench rows) still comes from the full replays and tools/legion_identity.sh.
#
# BUILD is a build dir holding legion_scenario of an -O2 (Debug -O2 -g) or Release tree; BOTH trees need the tool
# files of this commit (map named, TAK_SITUATION_CMDS): cherry-pick the commit onto a base/candidate that predates it.
#   --base DIR --cand DIR   the two build dirs (required)
#   --sits LIST             comma list of situations (default: every tools/scenarios/we-timing/*.scn.gz)
#   --rounds N              swapped-pair rounds (default 2)
#   --warm N                warm-up ticks, dropped (default 1500: the rebuilt world needs ~1000 ticks to settle its
#                           Retail waypoint queues; see docs/development.md)
#   --ticks N               ticks measured after the warm-up (default 1000)
#   --counters              also collect TAK_SIMSTATS work counters (adds instrument cost: not for ms comparisons)
#   --data DIR              game install (default $TAK_DATA or ~/TAK/assets/game)
#   --out DIR               logs + report (default $TMPDIR/we-timing-<date>)
# Every timed run goes through /home/pocket_geek/tak-tmp/tools/timing.sh (flock; cores 0-3): one pair on 0-1 + 2-3.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
BASE= CAND= SITS= ROUNDS=2 WARM=1500 TICKS=1000 COUNTERS= INNER=
DATA=${TAK_DATA:-$HOME/TAK/assets/game}; OUT=
TIMING=${TAK_TIMING_SH:-/home/pocket_geek/tak-tmp/tools/timing.sh}
while [ $# -gt 0 ]; do case $1 in
  --base) BASE=$2; shift 2;; --cand) CAND=$2; shift 2;; --sits) SITS=$2; shift 2;; --rounds) ROUNDS=$2; shift 2;;
  --warm) WARM=$2; shift 2;; --ticks) TICKS=$2; shift 2;; --counters) COUNTERS=1; shift;; --data) DATA=$2; shift 2;;
  --out) OUT=$2; shift 2;; --inner) INNER=1; shift;; *) echo "we_timing.sh: unknown option $1" >&2; exit 2;; esac; done
[ -n "$BASE" ] && [ -n "$CAND" ] || { sed -n 2,22p "$0" >&2; exit 2; }
for d in "$BASE" "$CAND"; do [ -x "$d/legion_scenario" ] || { echo "we_timing.sh: $d/legion_scenario missing" >&2; exit 2; }; done
OUT=${OUT:-${TMPDIR:-/tmp}/we-timing-$(date +%m%d-%H%M)}
mkdir -p "$OUT/scn" "$OUT/runs"

# Situations, decompressed once.
if [ -z "$INNER" ]; then
  if [ -z "$SITS" ]; then SITS=$(cd "$HERE/scenarios/we-timing" && ls *.scn.gz | sed 's/\.scn\.gz$//' | paste -sd,); fi
  for s in ${SITS//,/ }; do
    [ -s "$OUT/scn/$s.scn" ] || zcat "$HERE/scenarios/we-timing/$s.scn.gz" > "$OUT/scn/$s.scn"
  done
  echo "we_timing: base=$BASE cand=$CAND sits=$SITS rounds=$ROUNDS warm=$WARM out=$OUT"
  args=(--inner --base "$BASE" --cand "$CAND" --sits "$SITS" --rounds "$ROUNDS" --warm "$WARM" --data "$DATA" --out "$OUT")
  [ -n "$TICKS" ] && args+=(--ticks "$TICKS"); [ -n "$COUNTERS" ] && args+=(--counters)
  start=$(date +%s)
  "$TIMING" bash "$0" "${args[@]}"
  echo "we_timing: wall $(( $(date +%s) - start )) s (including any wait for the timing cores)"
  python3 "$HERE/we_timing.py" "$OUT" --warm "$WARM" | tee "$OUT/report.txt"
  exit 0
fi

# ---- inner: already on the timing cores, holding the lock ----
one() {   # one <build> <cores> <sit> <log>
  local mode=R; [[ $3 == L-* ]] && mode=L
  local ticks=$TICKS
  local t0=$(date +%s.%N)
  env TAK_PHASE=1 TAK_PHASE_MS=0 ${COUNTERS:+TAK_SIMSTATS=1} taskset -c "$2" "$1/legion_scenario" "$OUT/scn/$3.scn" \
      --mode $([ $mode = R ] && echo retail || echo legion) --data "$DATA" --ticks $((WARM + ticks)) --offsets 0 \
      --no-observer --workers 2> "$4.err" > "$4.out" || { echo "run failed: $4 ($(tail -1 "$4.err"))" >&2; return 0; }
  grep -E "^SIMPHASE|^SIMSTATS" "$4.err" > "$4" || true
  echo "wall $(echo "$(date +%s.%N) - $t0" | bc)" >> "$4.out"; rm -f "$4.err"
}
for r in $(seq 1 "$ROUNDS"); do
  for s in ${SITS//,/ }; do
    p="$OUT/runs/r$r-$s"
    one "$BASE" 0-1 "$s" "$p-base-01" & one "$CAND" 2-3 "$s" "$p-cand-23" & wait
    one "$CAND" 0-1 "$s" "$p-cand-01" & one "$BASE" 2-3 "$s" "$p-base-23" & wait
    echo "round $r $s done"
  done
done
