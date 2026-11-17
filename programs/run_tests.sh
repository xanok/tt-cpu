#!/bin/bash
# Assemble every example with cpu8asm, run it in cpu8sim and compare the
# output with the expectations in the program's header comments:
#   ; expect: 1 2 3          OUT values (decimal)
#   ; expect-text: Hi\n      OUT values as text (\n, \t escapes)
#   ; input: 0xB5            value on the input port
#   ; stdin: Ana\n42\n        text for the input mailbox (lib/input.asm)
#   ; limit: N               endless program: run N instructions, compare
#                            the first outputs with "expect"
# Usage: ./run_tests.sh [prog.asm ...]
cd "$(dirname "$0")" || exit 1
make -s -C ../tools cpu8asm cpu8sim || exit 1
mkdir -p build
ASM=../tools/cpu8asm
SIM=../tools/cpu8sim

pass=0
fail=0
progs=("$@")
[ ${#progs[@]} -eq 0 ] && progs=(*.asm)

hdr() { sed -n "s/^; *$1: *//p" "$2" | head -1; }

for f in "${progs[@]}"; do
    name=$(basename "$f" .asm)
    input=$(hdr input "$f")
    limit=$(hdr limit "$f")
    stdin=$(hdr stdin "$f")
    expect=$(hdr expect "$f")
    text=$(hdr expect-text "$f")
    result=FAIL
    detail=""
    args=()
    [ -n "$input" ] && args+=(-i "$input")
    [ -n "$limit" ] && args+=(-n "$limit")
    [ -n "$stdin" ] && args+=(-I "$stdin")

    if ! $ASM -l "build/$name.lst" -o "build/$name.bin" "$f"; then
        detail="assembler error"
    elif [ -z "$expect" ] && [ -z "$text" ]; then
        detail="no expect/expect-text header"
    else
        if [ -n "$text" ]; then
            printf '%b' "$text" > "build/$name.want"
            $SIM -a "${args[@]}" "build/$name.bin" > "build/$name.got" 2> "build/$name.err"
        else
            echo "$expect" | tr -s ' ' '\n' > "build/$name.want"
            $SIM "${args[@]}" "build/$name.bin" > "build/$name.got" 2> "build/$name.err"
        fi
        status=$?
        if [ -n "$limit" ]; then
            want_status=3
            head -c "$(stat -c %s "build/$name.want")" "build/$name.got" > "build/$name.cut"
            mv "build/$name.cut" "build/$name.got"
        else
            want_status=0
        fi
        if [ $status -ne $want_status ]; then
            detail="exit status $status (expected $want_status): $(tail -2 "build/$name.err" | tr '\n' ' ')"
        elif ! cmp -s "build/$name.want" "build/$name.got"; then
            detail="output differs: got [$(tr '\n' ' ' < "build/$name.got")] want [$(tr '\n' ' ' < "build/$name.want")]"
        else
            result=PASS
            size=$(stat -c %s "build/$name.bin")
            clocks=$($SIM -q -s "${args[@]}" "build/$name.bin" 2>&1 | sed -n 's/.* \([0-9]*\) clocks.*/\1/p')
            detail="$size bytes, $clocks clocks"
        fi
    fi
    printf '%-14s %s  %s\n' "$name" "$result" "$detail"
    if [ $result = PASS ]; then pass=$((pass + 1)); else fail=$((fail + 1)); fi
done
echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
