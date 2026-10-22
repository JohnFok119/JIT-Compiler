#!/usr/bin/env bash
# Builds the CLI, runs --bench for every expression in bench/suite.txt pinned to
# one CPU, and writes results.csv + machine.txt.
# OUT=dir overrides the output directory (default docs/bench); N=count overrides
# the evaluations per run (default 1000000); CPU=n overrides the pinned core (default 2).
set -euo pipefail
cd "$(dirname "$0")/.."

OUT=${OUT:-docs/bench}
N=${N:-1000000}
CPU=${CPU:-2}
mkdir -p "$OUT"

make -s all

{
	echo "cpu: $(grep -m1 'model name' /proc/cpuinfo | sed 's/^[^:]*: *//')"
	echo "gcc: $(gcc --version | head -n1)"
	echo "kernel: $(uname -sr)"
	echo "pinned: taskset -c $CPU"
	echo "evaluations per run: $N"
} > "$OUT/machine.txt"

echo "expr,backend,compile_us,ns_per_eval" > "$OUT/results.csv"
grep -v '^#' bench/suite.txt | grep -v '^[[:space:]]*$' | while IFS= read -r line; do
	name=${line%% *}
	expr=${line#* }
	echo "== $name" >&2
	table=$(taskset -c "$CPU" build/jit --bench "$N" "$expr")
	echo "$table" >&2
	echo "$table" |
		awk -v e="$name" 'NR > 1 { printf "%s,%s,%s,%s\n", e, $1, $2, $3 }' >> "$OUT/results.csv"
done
