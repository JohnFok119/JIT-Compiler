#!/usr/bin/env bash
# REPL and colour tests. They need a terminal, so the program runs under
# `script`, which gives it a pseudo-terminal. Run from anywhere:
#   bash tests/check-repl.sh
# Skipped (exit 0) when `script` is missing.
cd "$(dirname "$0")/.." || exit 1

if ! command -v script > /dev/null; then
	echo "repl: script(1) not found, skipped"
	exit 0
fi

fail=0
count=0

# tty INPUT COMMAND: run COMMAND on a pseudo-terminal, typing INPUT into it.
# script also logs the session to a file. The typing waits (up to 10 s) until
# the program has shown its first "jit> " prompt, or, when there is no input,
# until it has printed anything.
tty() {
	local log
	log=$(mktemp)
	{
		local i
		for i in $(seq 100); do
			if [ -n "$1" ]; then
				grep -q 'jit> ' "$log" && break
			else
				grep -qv '^Script started' "$log" && break
			fi
			sleep 0.1
		done
		printf '%b' "$1"
	} | timeout 60 script -qfc "stty -echo; $2" "$log" | tr -d '\r'
	rm -f "$log"
}

# expect NAME ACTUAL EXPECTED
expect() {
	local a e
	count=$((count + 1))
	a=$(printf '%s\n' "$2" | sed 's/ *$//')
	e=$(printf '%s\n' "$3" | sed 's/ *$//')
	if [ "$a" = "$e" ]; then
		echo "ok repl-$1"
	else
		echo "FAIL repl-$1"
		diff <(printf '%s\n' "$e") <(printf '%s\n' "$a")
		fail=1
	fi
}

# contains NAME ACTUAL NEEDLE
contains() {
	count=$((count + 1))
	if [[ "$2" == *"$3"* ]]; then
		echo "ok repl-$1"
	else
		echo "FAIL repl-$1: missing $(printf '%q' "$3") in $(printf '%q' "$2")"
		fail=1
	fi
}

# expressions, name=value tokens, errors, blank lines, :q
out=$(tty '1+2\n\nx*2 x=4\n(1\n:q\n2+2\n' 'NO_COLOR=1 build/jit --backend interp 2>&1')
expect basic "$out" "jit> 3
jit> jit> 8
jit> error: expected ')'
  (1
    ^
jit> "

# a word without '=' among the name=value tokens is an error, not a crash
out=$(tty 'x+y x=1 y\n:q\n' 'NO_COLOR=1 build/jit --backend interp 2>&1')
expect bad-binding "$out" "jit> error: 'y' is not name=value
jit> "

# EOF ends the session cleanly
out=$(tty '5*5\n' 'NO_COLOR=1 build/jit --backend interp 2>&1; echo status=$?')
expect eof "$out" "jit> 25
jit>
status=0"

# :emit toggles a stage dump: shown, then the value; second :emit turns it off
out=$(tty ':emit ast\n2*3\n:emit ast\n2*3\n:emit bogus\n:nope\n:q\n' 'NO_COLOR=1 build/jit --backend interp 2>&1')
expect emit "$out" "jit> emit ast
jit> (mul
  (num 2)
  (num 3))
6
jit> emit off
jit> 6
jit> error: usage: :emit tokens|ast|ir|infix|asm|hex|c|wasm
jit> error: unknown command (try :q or :emit STAGE)
jit> "

# the x86 and gcc backends in the REPL, and the c stage
out=$(tty ':emit c\nx+1 x=2\n' 'NO_COLOR=1 build/jit --backend gcc 2>&1')
contains emit-c "$out" "double evaluate(const double *v) {"
contains emit-c-result "$out" "
3
"

# options given before the (missing) expression still apply
out=$(tty '10/4\n:q\n' 'NO_COLOR=1 build/jit --legacy 2>&1')
contains legacy "$out" "0.924142"

# colour: red "error:" and a dim caret line on a terminal ...
out=$(tty '' "build/jit '(1' 2>&1; true")
contains color-red "$out" $'\033[31merror:\033[0m expected'
contains color-dim "$out" $'\033[2m'
# ... but not with NO_COLOR set
out=$(tty '' "NO_COLOR=1 build/jit '(1' 2>&1; true")
contains nocolor "$out" "error: expected ')'"
if [[ "$out" == *$'\033'* ]]; then
	echo "FAIL repl-nocolor-escape"
	fail=1
fi
# and not when stderr is a pipe
count=$((count + 1))
if build/jit '(1' 2>&1 | grep -q $'\033'; then
	echo "FAIL repl-pipe-no-color"
	fail=1
else
	echo "ok repl-pipe-no-color"
fi

echo "repl: $count cases"
exit $fail
