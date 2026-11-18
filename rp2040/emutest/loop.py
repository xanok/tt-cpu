#!/usr/bin/env python3
"""Loopback test of the SPI RAM emulator on a real Pico 2 (see README.md).

The Pico 2 runs build_pico2/loop_test.uf2 (or loop_test_stock.uf2) with four
jumper wires: GP2-GP16 (CS), GP3-GP19 (SCK), GP4-GP17 (MOSI), GP18-GP5 (MISO).
For every program, this script records the chip's SPI waveform from the
Verilog (build/tr_fall/trace, build/tr_rise/trace: "make setup"), plays it on
the Pico 2 and checks every MISO bit the chip would sample, and the RAM after.

  ./loop.py                      every halting example, both straps, ratio 15
  ./loop.py -r 15,10,8 hello     some ratios, some programs
  ./loop.py --sweep              ratio 40 down to 6, bubble_sort and factorial

ratio = system clocks per chip clock: 15 is 10 MHz at the 150 MHz system clock.
Only the Python standard library is used. SPDX-License-Identifier: Apache-2.0
"""
import argparse, glob, os, re, subprocess, sys, termios, time

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.normpath(os.path.join(HERE, '..', '..'))
PROG = os.path.join(HERE, 'build', 'prog')


class Port:
    def __init__(self, dev):
        self.fd = os.open(dev, os.O_RDWR | os.O_NOCTTY)
        a = termios.tcgetattr(self.fd)
        a[0] = a[1] = a[3] = 0                    # raw: no echo, no line editing, no CR/LF changes
        a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        a[6][termios.VMIN], a[6][termios.VTIME] = 0, 1
        termios.tcsetattr(self.fd, termios.TCSANOW, a)
        termios.tcflush(self.fd, termios.TCIOFLUSH)
        self.buf = b''

    def cmd(self, text, timeout=5.0):
        os.write(self.fd, text.encode() + b'\n')
        end = time.time() + timeout
        while b'\n' not in self.buf:
            if time.time() > end:
                raise RuntimeError(f'no answer to {text[:20]!r}')
            self.buf += os.read(self.fd, 65536)
        ln, self.buf = self.buf.split(b'\n', 1)
        ln = ln.decode().strip()
        if not ln.startswith('OK'):
            raise RuntimeError(f'{text[:20]!r}: {ln}')
        return ln[3:]


def record(name):
    """assemble and record both waveforms (like run.sh)"""
    src = os.path.join(TOP, 'programs', name + '.asm')
    m = re.search(r'^; *input: *(\S+)', open(src).read(), re.M)
    os.makedirs(PROG, exist_ok=True)
    binf = os.path.join(PROG, name + '.bin')
    subprocess.run([os.path.join(TOP, 'tools', 'cpu8asm'), '-o', binf, src], check=True)
    for s in ('fall', 'rise'):
        subprocess.run([os.path.join(HERE, 'build', 'tr_' + s, 'trace'), binf,
                        os.path.join(PROG, f'{name}.{s}.txt'), os.path.join(PROG, f'{name}.{s}.final'),
                        m.group(1) if m else '0'], check=True, stdout=subprocess.DEVNULL)


def run_one(port, name, strap, ratio):
    prog = open(os.path.join(PROG, name + '.bin'), 'rb').read()[:4096]
    final = open(os.path.join(PROG, f'{name}.{strap}.final'), 'rb').read()
    lines = [l for l in open(os.path.join(PROG, f'{name}.{strap}.txt')).read().split('\n') if len(l) >= 5]
    n = len(lines)
    ns = (n + 31) // 32                        # sample words
    nd = (ns * 32 + 20 + 9) // 10              # drive words: all samples, then 20 idle clocks
    bits = [(l[0] == '1') | (l[1] == '1') << 1 | (l[2] == '1') << 2 for l in lines]
    bits += [1] * (nd * 10 - n)                # idle: CS high
    words = []
    for w in range(nd):
        v = 0
        for j in range(10):
            v |= bits[w * 10 + j] << (3 * j)
        words.append(v)
    port.cmd('P ' + prog.hex())
    for w in range(0, nd, 256):
        port.cmd(f'D {w:x} ' + ''.join(f'{v:08x}' for v in words[w:w + 256]))
    r = port.cmd(f'RUN {nd} {ns} {ratio}').split()
    us, stall = int(r[0]), int(r[1])
    samp = []
    for w in range(0, ns, 256):
        h = port.cmd(f'S {w:x} {min(256, ns - w):x}')
        samp += [int(h[i:i + 8], 16) for i in range(0, len(h), 8)]
    checked = wrong = 0
    first = -1
    for k, l in enumerate(lines):
        if l[3] == '1':
            checked += 1
            if (samp[k >> 5] >> (k & 31) & 1) != int(l[4]):
                wrong += 1
                if first < 0:
                    first = k
    mem = b''
    for a in range(0, 4096, 1024):
        mem += bytes.fromhex(port.cmd(f'M {a:x} 400'))
    memdiff = sum(1 for a in range(4096) if mem[a] != final[a])
    stray = int(port.cmd('Z'))
    ok = checked > 0 and wrong == 0 and memdiff == 0 and stray == 0 and not stall
    msg = (f'{"ok  " if ok else "FAIL"} {n} chip clocks, {checked} MISO bits checked, {wrong} wrong, '
           f'memory {memdiff} bytes differ' + (f', {stray} stray writes' if stray else '') +
           (', drive FIFO ran dry: timing not kept' if stall else '') +
           (f'; first wrong bit at chip clock {first}' if first >= 0 else '') + f'  ({us / 1000:.1f} ms)')
    return ok, msg


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('programs', nargs='*', help='program names (default: every halting example)')
    ap.add_argument('-d', '--dev', default='/dev/ttyACM0')
    ap.add_argument('-r', '--ratio', default='15', help='system clocks per chip clock, comma list')
    ap.add_argument('-s', '--strap', default='fall,rise', help='fall (MISO_RISE=0), rise (=1), or both')
    ap.add_argument('--sweep', action='store_true', help='ratio 40 down to 6 on bubble_sort and factorial')
    o = ap.parse_args()
    for t in ('tr_fall', 'tr_rise'):
        if not os.path.exists(os.path.join(HERE, 'build', t, 'trace')):
            sys.exit(f'build/{t}/trace is missing: run "make setup" in {HERE} first')
    subprocess.run(['make', '-s', '-C', os.path.join(TOP, 'tools'), 'cpu8asm'], check=True)
    if o.programs:
        names = o.programs
    elif o.sweep:
        names = ['bubble_sort', 'factorial']
    else:
        names = []
        for f in sorted(glob.glob(os.path.join(TOP, 'programs', '*.asm'))):
            t = open(f).read()
            if re.search(r'^; *(limit|stdin):', t, re.M):
                continue                              # endless, or needs the keyboard feeder
            names.append(os.path.basename(f)[:-4])
    ratios = [40, 25, 20, 16, 15, 12, 10, 8, 7, 6] if o.sweep else [int(x) for x in o.ratio.split(',')]
    for n in names:
        record(n)
    port = Port(o.dev)
    info = port.cmd('ID')
    print('Pico 2:', info)
    sysclk = int(re.search(r'sys=(\d+)', info).group(1))
    passed = failed = 0
    for ratio in ratios:
        print(f'ratio {ratio}: chip clock {sysclk / ratio / 1e6:.2f} MHz')
        for strap in o.strap.split(','):
            for n in names:
                ok, msg = run_one(port, n, strap, ratio)
                passed += ok
                failed += not ok
                print(f'  {n:<12} strap {0 if strap == "fall" else 1}  {msg}', flush=True)
    print(f'{passed} passed, {failed} failed')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
