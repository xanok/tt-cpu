// Replays the chip's SPI waveform (recorded by ./trace from the Verilog)
// into rp2040js running the emu_test firmware, i.e. the real sram.c and
// sram.pio of the SPI RAM emulator. Checks every MISO bit the chip would
// sample, and the RAM contents at the end.
//
//   node replay.mjs firmware.bin prog.bin trace.txt final.bin [options]
//     --ratio N     RP2040 system clocks per chip clock (default 20:
//                   200 MHz system clock, 10 MHz chip clock)
//     --delay D     total pad/multiplexer delay of the loop (chip to
//                   RP2040 and back), in system clocks
//     --quiet       one result line only
// SPDX-License-Identifier: Apache-2.0
import fs from 'node:fs';
import { RP2040, GPIOPinState, FIFO } from 'rp2040js';
import { bootromB1 } from './build/bootrom.mjs';

const PIN_CS = 21, PIN_MOSI = 22, PIN_MISO = 23, PIN_SCK = 24, PIN_READY = 25;
const RAM_OFFSET = 0x30000;   // emu_ram at 0x20030000

const args = process.argv.slice(2);
const opt = { ratio: 20, delay: 0, quiet: false, dump: 0 };
const files = [];
for (let i = 0; i < args.length; i++) {
  if (args[i] === '--ratio') opt.ratio = Number(args[++i]);
  else if (args[i] === '--delay') opt.delay = Number(args[++i]);
  else if (args[i] === '--quiet') opt.quiet = true;
  else if (args[i] === '--dump') opt.dump = Number(args[++i]);
  else files.push(args[i]);
}
if (files.length !== 4 || !(opt.ratio >= 2) || !(opt.delay >= 0) || opt.delay >= 1000) {
  console.error('usage: node replay.mjs firmware.bin prog.bin trace.txt final.bin [--ratio N] [--delay D] [--quiet]');
  process.exit(2);
}
const [fwFile, progFile, traceFile, finalFile] = files;

const mcu = new RP2040();
mcu.loadBootrom(bootromB1);
mcu.flash.set(fs.readFileSync(fwFile), 0);
mcu.core.PC = 0x10000000;
// rp2040js normally runs the PIO state machines from a JavaScript timer,
// unrelated to the CPU. Here everything is stepped together, one system
// clock at a time.
for (const pio of mcu.pio) pio.run = () => {};

// Three behaviours of the real PIO that rp2040js 1.4.2 lacks and that the
// emulator relies on (RP2040 datasheet, CTRL and SMx_SHIFTCTRL):
//  - a state machine whose CTRL.SM_ENABLE bit is 0 does not run. rp2040js
//    steps all four regardless; the emulator disables a machine, restarts
//    it and jumps it to its first instruction before every transaction.
//  - changing FJOIN_TX / FJOIN_RX flushes both FIFOs. pio_sm_clear_fifos()
//    works this way; without it, words the DMA pre-loaded for one READ are
//    still there for the next one.
//  - FJOIN_TX makes the TX FIFO 8 words deep.
for (const pio of mcu.pio) {
  for (const sm of pio.machines) {
    let shiftCtrl = sm.shiftCtrl;
    Object.defineProperty(sm, 'shiftCtrl', {
      get: () => shiftCtrl,
      set: (value) => {
        const changed = (shiftCtrl ^ value) >>> 30;
        shiftCtrl = value;
        if (changed) {
          const depth = (value >>> 30) & 1 ? 8 : 4;
          if (sm.txFIFO.size !== depth) sm.txFIFO = new FIFO(depth);
          sm.txFIFO.reset();
          sm.rxFIFO.reset();
          mcu.dma.setDREQ(sm.dreqTx);
          mcu.dma.clearDREQ(sm.dreqRx);
        }
      },
    });
  }
}

const cycleNanos = 1e9 / 125e6;  // rp2040js's notion of one system clock
let cpuDebt = 0;
function sysClock() {
  if (cpuDebt <= 0) {
    if (mcu.core.waiting) cpuDebt = 1;
    else cpuDebt += mcu.core.executeInstruction();
  }
  cpuDebt--;
  for (const pio of mcu.pio) {
    for (const sm of pio.machines) if (sm.enabled) sm.step();
    pio.checkChangedPins();
  }
  mcu.clock.tick(cycleNanos);
  sysNow++;
  if (process.env.DEBUG_PC && sysNow >= Number(process.env.DEBUG_PC) && sysNow < Number(process.env.DEBUG_PC) + Number(process.env.DEBUG_N || 60)) {
    const r = mcu.pio[1].machines[1];
    console.log(`    t=${sysNow} rd pc=${r.pc} waiting=${r.waiting} wt=${r.waitType} wi=${r.waitIndex} wp=${r.waitPolarity} inBase=${r.inBase} en=${r.enabled} x=${r.x} isrcnt=${r.inputShiftCount} isr=${(r.inputShiftReg >>> 0).toString(16)} cs=${+mcu.gpio[PIN_CS].inputValue} sck=${+mcu.gpio[PIN_SCK].inputValue} mosi=${+mcu.gpio[PIN_MOSI].inputValue}`);
  }
}
function setPins(cs, sck, mosi) {
  mcu.gpio[PIN_CS].setInputValue(cs);
  mcu.gpio[PIN_SCK].setInputValue(sck);
  mcu.gpio[PIN_MOSI].setInputValue(mosi);
}
function miso() {
  const v = mcu.gpio[PIN_MISO].value;
  return v === GPIOPinState.High ? 1 : v === GPIOPinState.Low ? 0 : -1;  // -1: not driven
}

if (process.env.DEBUG_RX) {
  const r = mcu.pio[1].machines[1];
  const pull = r.rxFIFO.pull.bind(r.rxFIFO), push = r.rxFIFO.push.bind(r.rxFIFO);
  r.rxFIFO.pull = () => { const v = pull(); console.log(`    rx pull ${(v >>> 0).toString(16)} cpu=${mcu.core.PC.toString(16)}`); return v; };
  r.rxFIFO.push = (v) => { console.log(`    rx push ${(v >>> 0).toString(16)} pc=${r.pc} isrcnt=${r.inputShiftCount}`); push(v); };
}
let sysNow = 0;
if (process.env.DEBUG_ARM) {
  const r = mcu.pio[1].machines[1], w = mcu.pio[0].machines[1];
  for (const [name, m] of [['read', r], ['write', w]]) {
    const restart = m.restart.bind(m);
    m.restart = () => { console.log(`    t=${sysNow} ${name} SM restart`); restart(); };
    let en = m.enabled;
    Object.defineProperty(m, 'enabled', { get: () => en, set: (v) => { if (v !== en) console.log(`    t=${sysNow} ${name} SM enabled=${v}`); en = v; } });
  }
  const cs = mcu.gpio[PIN_CS], set = cs.setInputValue.bind(cs);
  let last = true;
  cs.setInputValue = (v) => { if (v !== last) console.log(`    t=${sysNow} CS ${v ? 'high' : 'low'}`); last = v; set(v); };
}
// Boot: SPI idle until the firmware reports that the emulator is armed.
setPins(true, false, false);
let boot = 0;
while (mcu.gpio[PIN_READY].value !== GPIOPinState.High) {
  sysClock();
  if (++boot > 50_000_000) {
    console.error('firmware did not reach the ready state');
    process.exit(2);
  }
}
for (let i = 0; i < 2000; i++) sysClock();

// Load the program, like the firmware's W command does.
const prog = fs.readFileSync(progFile);
mcu.sram.fill(0, RAM_OFFSET, RAM_OFFSET + 0x10000);
mcu.sram.set(prog.subarray(0, 4096), RAM_OFFSET);

const lines = fs.readFileSync(traceFile, 'latin1').split('\n').filter((l) => l.length >= 5);
let checked = 0, wrong = 0, undriven = 0, firstBad = -1;
// Pad delay: the emulated RP2040 sees the chip's pins without delay here, so
// a loop delay of D system clocks (chip -> RP2040 plus RP2040 -> chip) means
// the chip's flip-flop captures what MISO was D clocks BEFORE the sampling
// edge. MISO is recorded every system clock for that.
const HIST = 1024;
const hist = new Int8Array(HIST);
for (let k = 0; k < lines.length; k++) {
  const l = lines[k];
  setPins(l[0] === '1', l[1] === '1', l[2] === '1');
  for (let c = 0; c < opt.ratio; c++) {
    sysClock();
    hist[sysNow % HIST] = miso();
  }
  if (l[3] === '1') {   // the clock edge that ends this period samples MISO
    const got = hist[(sysNow - opt.delay + HIST) % HIST];
    const expect = Number(l[4]);
    checked++;
    if (got !== expect) {
      wrong++;
      if (got < 0) undriven++;
      if (firstBad < 0) firstBad = k;
      if (opt.dump-- > 0) console.log(`  chip clock ${k}: expected ${expect}, MISO ${got}`);
    }
  }
  if (process.env.DEBUG_FROM && k >= Number(process.env.DEBUG_FROM) && k < Number(process.env.DEBUG_TO)) {
    const w = mcu.pio[0].machines[1], r = mcu.pio[1].machines[1];
    console.log(`k=${k} pins=${l.slice(0, 3)} miso=${miso()} | wr pc=${w.pc} osrcnt=${w.outputShiftCount} txf=${w.txFIFO.itemCount} | rd pc=${r.pc} rxf=${r.rxFIFO.itemCount}`);
  }
}
// let a last write land
for (let i = 0; i < 400; i++) sysClock();

const final = fs.readFileSync(finalFile);
let memDiff = 0, firstMem = -1;
for (let a = 0; a < 4096; a++) {
  if (mcu.sram[RAM_OFFSET + a] !== final[a]) {
    memDiff++;
    if (firstMem < 0) firstMem = a;
  }
}
// nothing outside the 4 KB may be written
let stray = 0;
for (let a = 4096; a < 0x10000; a++) if (mcu.sram[RAM_OFFSET + a] !== 0) stray++;

const ok = wrong === 0 && memDiff === 0 && stray === 0 && checked > 0;
let msg = `${ok ? 'ok  ' : 'FAIL'} ratio ${opt.ratio} delay ${opt.delay}: ${lines.length} chip clocks, ` +
  `${checked} MISO bits checked, ${wrong} wrong` + (undriven ? ` (${undriven} not driven)` : '') +
  `, memory ${memDiff} bytes differ` + (stray ? `, ${stray} stray writes` : '');
if (!ok && firstBad >= 0) msg += `; first wrong bit at chip clock ${firstBad}`;
if (!ok && firstMem >= 0) msg += `; first memory difference at 0x${firstMem.toString(16)}`;
console.log(msg);
process.exit(ok ? 0 : 1);
