#!/usr/bin/env bash
# Golden tests: run from repo root. For each tests/golden/NAME.args runs
# build/jit with those args and compares stdout and exit status, and stderr
# too when tests/golden/NAME.err exists.
cd "$(dirname "$0")/.." || exit 1
JIT=${JIT:-build/jit}

fail=0
count=0
for argsfile in tests/golden/*.args; do
	name=$(basename "$argsfile" .args)
	mapfile -t args < "$argsfile"
	expected_code=$(cat "tests/golden/$name.code")
	actual_out=$(mktemp)
	actual_err=$(mktemp)
	"$JIT" "${args[@]}" > "$actual_out" 2> "$actual_err" < /dev/null
	actual_code=$?
	count=$((count + 1))
	err_ok=1
	if [ -e "tests/golden/$name.err" ] &&
	   ! cmp -s "$actual_err" "tests/golden/$name.err"; then
		err_ok=0
	fi
	if cmp -s "$actual_out" "tests/golden/$name.out" &&
	   [ "$actual_code" = "$expected_code" ] && [ "$err_ok" = 1 ]; then
		echo "ok $name"
	else
		echo "FAIL $name (exit $actual_code, expected $expected_code)"
		diff "tests/golden/$name.out" "$actual_out"
		if [ -e "tests/golden/$name.err" ]; then
			diff "tests/golden/$name.err" "$actual_err"
		fi
		fail=1
	fi
	rm -f "$actual_out" "$actual_err"
done
echo "golden: $count cases"
exit $fail
