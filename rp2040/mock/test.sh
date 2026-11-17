#!/bin/sh
# End-to-end test of cpu8load against the mock firmware (real parser,
# fake hardware) over a pseudo-terminal. Run: make test
set -u
cd "$(dirname "$0")"
TMP=$(mktemp -d)
PTY=$TMP/tty
fails=0

./cpu8mock -l "$PTY" > "$TMP/mock.log" 2>&1 &
MOCK=$!
trap 'kill $MOCK 2>/dev/null; rm -rf "$TMP"' EXIT
for i in 1 2 3 4 5 6 7 8 9 10; do [ -e "$PTY" ] && break; sleep 0.1; done

check() {  # check NAME EXPECTED_EXIT PATTERN -- command...
    name=$1 want=$2 pat=$3; shift 4
    "$@" > "$TMP/out" 2>&1
    got=$?
    if [ "$got" -ne "$want" ] || ! grep -q -- "$pat" "$TMP/out"; then
        echo "FAIL $name (exit $got, want $want; pattern '$pat')"
        sed 's/^/    /' "$TMP/out"
        fails=$((fails + 1))
    else
        echo "ok   $name"
    fi
}

# Real cpu8 programs (the mock board runs them with the cpu8sim core).
ASM=../../tools/cpu8asm
SIM=../../tools/cpu8sim
make -s -C ../../tools cpu8asm cpu8sim || exit 1
printf 'IN\nXRI 0xFF\nSTA 0xFFE\nLDI 0x77\nSTA 0xFFF\nOUT\nHLT\n' > "$TMP/inv.asm"
$ASM -o "$TMP/inv.bin" "$TMP/inv.asm" || exit 1
$ASM -o "$TMP/count.bin" ../../programs/count.asm || exit 1
$ASM -o "$TMP/hello.bin" ../../programs/hello.asm || exit 1
printf 'loop: JMP loop\n' > "$TMP/forever.asm"
$ASM -o "$TMP/forever.bin" "$TMP/forever.asm" || exit 1

# A 300-byte "program" with known contents (upload/verify tests only).
python3 -c 'import sys; sys.stdout.buffer.write(bytes((i * 7 + 3) & 255 for i in range(300)))' > "$TMP/prog.bin"
head -c 4097 /dev/zero > "$TMP/big.bin"
: > "$TMP/empty.bin"

L="./cpu8load -d $PTY"
check "help"            0 "usage: cpu8load"          -- ./cpu8load -h
check "no args"         1 "usage: cpu8load"          -- ./cpu8load
check "missing file"    1 "cannot open nope.bin"     -- $L nope.bin
check "empty file"      1 "is empty"                 -- $L "$TMP/empty.bin"
check "too big"         1 "too big"                  -- $L "$TMP/big.bin"
check "bad option val"  1 "bad value 'zz' for -f"    -- $L -f zz "$TMP/prog.bin"
check "not a tty"       1 "not a serial port"        -- ./cpu8load -d /dev/null "$TMP/prog.bin"
check "no device"       1 "cannot open /dev/nonexist" -- ./cpu8load -d /dev/nonexistent "$TMP/prog.bin"
check "clock too fast"  1 "too fast"                 -- $L -f 50000000 "$TMP/prog.bin"
check "load+run"        0 "loaded 300 bytes"         -- $L "$TMP/prog.bin"
check "wait halt"       0 "uo_out = 0x09 (9)  halted = yes" -- $L -p 3 -w "$TMP/count.bin"
check "monitor events"  0 "uo_out = 0x03"            -- $L -f 5000 -w -m "$TMP/count.bin"
grep -q "HALTED" "$TMP/out" || { echo "FAIL monitor shows HALTED"; fails=$((fails + 1)); }
check "ram dump"        0 "0FF0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A5 77" \
                                                     -- $L -w -i 0x5A -r 0xFF0:16 "$TMP/inv.bin"
check "dump program"    0 "0000: 03 0A 11 18"        -- $L -r 0:200 "$TMP/prog.bin"
check "timeout"         2 "did not halt within 1 s"  -- $L -w -t 1 "$TMP/forever.bin"
check "text output"     0 "^Hello, Tiny Tapeout!$"   -- $L -a "$TMP/hello.bin"
check "uneven clock"    0 "running at 9523809 Hz"    -- $L -f 9500000 "$TMP/prog.bin"
check "sysclk"          0 "sys=125000000"            -- $L -s 125000 -w "$TMP/count.bin"
check "edge strap"      0 "halted = yes"             -- $L -e 1 -w "$TMP/count.bin"
check "sysclk back"     0 "sys=200000000"            -- $L -s 200000 "$TMP/prog.bin"
check "bad -r"          1 "outside the 64 KB RAM"    -- $L -r 0xFFF0:32 "$TMP/prog.bin"

# Every halting example: the board's OUT output (-n) must equal cpu8sim's.
for f in ../../programs/*.asm; do
    name=$(basename "$f" .asm)
    grep -q '^; *limit:' "$f" && continue
    input=$(sed -n 's/^; *input: *//p' "$f" | head -1)
    $ASM -o "$TMP/$name.bin" "$f" || { fails=$((fails + 1)); continue; }
    $SIM -i "${input:-0}" "$TMP/$name.bin" > "$TMP/$name.sim"
    $L -n -i "${input:-0}" "$TMP/$name.bin" > "$TMP/$name.board" 2> "$TMP/$name.err"
    if cmp -s "$TMP/$name.sim" "$TMP/$name.board"; then
        echo "ok   example $name ($(wc -l < "$TMP/$name.sim") OUT bytes)"
    else
        echo "FAIL example $name: board output differs from cpu8sim"
        diff "$TMP/$name.sim" "$TMP/$name.board" | head -5 | sed 's/^/    /'
        sed 's/^/    /' "$TMP/$name.err" | tail -3
        fails=$((fails + 1))
    fi
done

# Raw protocol: error replies and resynchronisation.
python3 -I - "$PTY" > "$TMP/raw" 2>&1 <<'EOF'
import os, sys, termios, tty, time
fd = os.open(sys.argv[1], os.O_RDWR | os.O_NOCTTY)
tty.setraw(fd)
buf = b""
def cmd(line):
    global buf
    os.write(fd, line.encode() + b"\n")
    end = time.time() + 2
    while time.time() < end:
        while b"\n" in buf:
            l, buf = buf.split(b"\n", 1)
            l = l.decode().rstrip("\r")
            if l.startswith("OK") or l.startswith("ERR"):
                return l
        buf += os.read(fd, 4096)
    return "TIMEOUT"
tests = [
    ("PING", "OK cpu8-loader 1"),
    ("ping", "OK cpu8-loader 1"),
    ("FOO", "ERR unknown command 'FOO' (try HELP)"),
    ("R 0", "ERR R takes 2 argument(s)"),
    ("R 0 0", "ERR length must be 1..128"),
    ("R 0 129", "ERR 129 out of range (max 128)"),
    ("R 0xFFFF 2", "ERR range 0xffff+2 outside the 64 KB RAM"),
    ("W 0 ABC", "ERR need an even number of hex digits, max 128 bytes"),
    ("W 0 zz", "ERR bad hex data"),
    ("W 0x10 DEADbeef", "OK 4"),
    ("R 0x10 4", "OK DEADBEEF"),
    ("FILL 0x10 2 0x55", "OK"),
    ("R 0x10 4", "OK 5555BEEF"),
    ("UI 256", "ERR 256 out of range (max 255)"),
    ("UI 12x", "ERR bad number '12x'"),
    ("EDGE 2", "ERR 2 out of range (max 1)"),
    ("EDGE 1", "OK"),
    ("EDGE 0", "OK"),
    ("CLK 0", "OK 0"),
    ("GO", "ERR clock is stopped (send CLK first)"),
    ("CLK 1", "ERR 1 Hz too slow: min 12 Hz"),
    ("MON 2", "ERR 2 out of range (max 1)"),
    ("SYSCLK 10", "ERR sysclk must be 50000..250000 kHz"),
    ("X" * 400, "ERR line too long (max 320 characters)"),
    ("PING", "OK cpu8-loader 1"),
    ("HELP", "OK"),
    ("ST", "OK uo="),
]
bad = 0
for c, want in tests:
    got = cmd(c)
    if not got.startswith(want):
        print("FAIL raw %r: got %r want %r" % (c[:20], got, want)); bad += 1
print("raw protocol: %d/%d ok" % (len(tests) - bad, len(tests)))
sys.exit(1 if bad else 0)
EOF
if [ $? -eq 0 ]; then echo "ok   $(tail -1 "$TMP/raw")"; else cat "$TMP/raw"; fails=$((fails + 1)); fi

if [ $fails -eq 0 ]; then echo "ALL TESTS PASSED"; else echo "$fails TEST(S) FAILED"; exit 1; fi
