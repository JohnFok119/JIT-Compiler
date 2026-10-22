#!/usr/bin/env bash
# Encoder check: disassemble build/enc.bin (written by build/tests/test_encode)
# with objdump and compare mnemonics + operands, line by line, with the
# encoder's own listing build/enc.lst. Whitespace is normalised and objdump's
# "# 0x..." rip-target comments are dropped. Run from anywhere.
cd "$(dirname "$0")/.." || exit 1

if [ ! -s build/enc.bin ] || [ ! -s build/enc.lst ]; then
	echo "FAIL check-encoding: build/enc.bin or build/enc.lst missing (run build/tests/test_encode)"
	exit 1
fi

got=$(mktemp)
want=$(mktemp)
trap 'rm -f "$got" "$want"' EXIT

# Instruction lines are "addr:<TAB>bytes<TAB>text"; continuation lines of
# long instructions have no third field.
objdump -D -b binary -mi386:x86-64 -M intel build/enc.bin |
	awk -F'\t' '/^ *[0-9a-f]+:\t/ && NF >= 3 { print $3 }' |
	sed -e 's/ *#.*$//' -e 's/[[:space:]]\+/ /g' -e 's/^ //' -e 's/ $//' > "$got"
sed -e 's/[[:space:]]\+/ /g' -e 's/^ //' -e 's/ $//' build/enc.lst > "$want"

if diff "$want" "$got" > /dev/null; then
	echo "PASS check-encoding: $(wc -l < "$want") instructions match objdump"
	exit 0
fi
echo "FAIL check-encoding: encoder listing (<) differs from objdump (>):"
diff "$want" "$got" | head -40
exit 1
