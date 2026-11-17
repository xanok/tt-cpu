#!/bin/bash
# run.sh test|sweep [jobs]   (called by the Makefile)
cd "$(dirname "$0")" || exit 1
mode=$1; jobs=${2:-8}
ASM=../../tools/cpu8asm
make -s -C ../../tools cpu8asm || exit 1
mkdir -p build/prog
node="node replay.mjs"
quiet() { grep -v "not implemented\|Unimplemented"; }

record() {  # record NAME: assemble and record both waveforms
    local f=../../programs/$1.asm in
    in=$(sed -n 's/^; *input: *//p' "$f" | head -1)
    $ASM -o build/prog/$1.bin "$f" || return 1
    for m in fall rise; do
        build/tr_$m/trace build/prog/$1.bin build/prog/$1.$m.txt build/prog/$1.$m.final ${in:-0} > /dev/null || return 1
    done
}

if [ "$mode" = test ]; then
    names=()
    for f in ../../programs/*.asm; do
        grep -q '^; *limit:' "$f" && continue      # endless
        grep -q '^; *stdin:' "$f" && continue      # needs the firmware's mailbox feeder
        names+=("$(basename "$f" .asm)")
    done
    for n in "${names[@]}"; do record "$n" || { echo "cannot record $n"; exit 1; }; done
    fails=0
    for n in "${names[@]}"; do for m in fall rise; do echo "$n $m"; done; done |
    xargs -P "$jobs" -L 1 bash -c '
        r=$(node replay.mjs ../build_rp2040/emu_test.bin build/prog/$0.bin build/prog/$0.$1.txt build/prog/$0.$1.final 2>&1 | grep -v "not implemented\|Unimplemented")
        printf "%-12s strap %s  %s\n" "$0" "$([ $1 = fall ] && echo 0 || echo 1)" "$r"' | sort > build/test.out
    cat build/test.out
    fails=$(grep -c -v " ok " build/test.out)
    echo "$(grep -c " ok " build/test.out) passed, $fails failed"
    [ "$fails" -eq 0 ]
else
    # A program that reads and writes a lot, and one with CALL/RET (2-byte writes)
    progs="bubble_sort factorial"
    for n in $progs; do record "$n" || exit 1; done
    ratios="40 25 20 16 12 10 8 6"
    for cfg in "emu_test fall" "emu_test rise" "emu_test_stock rise"; do
        set -- $cfg
        echo
        echo "firmware $1, strap $([ $2 = fall ] && echo 0 || echo 1): ratio = RP2040 clocks per chip clock; d = pad delay of the whole loop in RP2040 clocks (one SCK period = 2 x ratio)"
        for r in $ratios; do
            for d in 0 $((r / 4)) $((r / 2)) $((r * 3 / 4)) $r $((r * 5 / 4)) $((r * 3 / 2)) $((r * 7 / 4)); do echo "$r $d"; done
        done | sort -u -k1,1n -k2,2n | xargs -P "$jobs" -L 1 bash -c '
            ok=1
            for n in '"$progs"'; do
                node replay.mjs ../build_rp2040/'"$1"'.bin build/prog/$n.bin build/prog/$n.'"$2"'.txt build/prog/$n.'"$2"'.final --ratio $0 --delay $1 > /dev/null 2>&1 || ok=0
            done
            echo "$0 $1 $ok"' | sort -k1,1nr -k2,2n |
        awk '{ if ($1 != r) { if (r) print line; r = $1; line = sprintf("  ratio %2d:", r) } line = line sprintf("  d=%-2d %s", $2, $3 ? "ok  " : "FAIL") } END { print line }'
    done
fi
