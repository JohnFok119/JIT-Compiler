#!/usr/bin/env bash
# Golden tests: run from repo root. For each tests/golden/NAME.args runs
# build/jit with those args and compares stdout and exit status.
cd "$(dirname "$0")/.." || exit 1

fail=0
count=0
for argsfile in tests/golden/*.args; do
	name=$(basename "$argsfile" .args)
	mapfile -t args < "$argsfile"
	expected_code=$(cat "tests/golden/$name.code")
	actual_out=$(mktemp)
	build/jit "${args[@]}" > "$actual_out" 2>/dev/null
	actual_code=$?
	count=$((count + 1))
	if cmp -s "$actual_out" "tests/golden/$name.out" &&
	   [ "$actual_code" = "$expected_code" ]; then
		echo "ok $name"
	else
		echo "FAIL $name (exit $actual_code, expected $expected_code)"
		diff "tests/golden/$name.out" "$actual_out"
		fail=1
	fi
	rm -f "$actual_out"
done
echo "golden: $count cases"
exit $fail
