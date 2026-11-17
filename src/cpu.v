/*
 * cpu8 - tiny 8-bit accumulator CPU with an SPI memory bus
 * SPDX-License-Identifier: Apache-2.0
 *
 * - Accumulator A, 12-bit index register X, stack pointer SP, flags Z and C,
 *   12-bit PC (4 KB address space). The stack lives in page 0xF
 *   (0xF00-0xFFF) and grows down from 0xFFF.
 * - Code and data share one external SPI RAM (23LC512-style, mode 0,
 *   READ=0x03 / WRITE=0x02, 16-bit address), e.g. the TT demo board's
 *   RP2040 running spi-ram-emu. SCK = clk/2.
 * - Every instruction is 2 bytes:  [op:4 | hi:4] [lo:8]
 *     ea  = {hi, lo}  (12-bit address)    imm = lo
 *   See docs/isa.md for the full table.
 */

`default_nettype none

module cpu8 (
    input  wire       clk,
    input  wire       rst_n,       // asynchronous; synchronised below
    input  wire       miso_rise,   // static strap: 0 = sample MISO at the end of the SCK high
                                   // phase, 1 = sample at the SCK rising edge
    input  wire [7:0] in_port,
    output reg  [7:0] out_port,
    output reg        out_strobe,  // toggles on every OUT (byte-ready for the host)
    output wire       halted,
    output reg        spi_cs_n,
    output reg        spi_sck,
    output wire       spi_mosi,
    input  wire       spi_miso
);

  localparam OP_IMM = 4'h0, OP_LDA = 4'h1, OP_STA = 4'h2, OP_ADD = 4'h3,
             OP_SUB = 4'h4, OP_LDX = 4'h5, OP_STX = 4'h6, OP_ADX = 4'h7,  // ea,X forms
             OP_CAL = 4'h8, OP_JMP = 4'h9, OP_JZ  = 4'hA, OP_JC  = 4'hB,
             OP_JNZ = 4'hC, OP_MSC = 4'hD, OP_CMP = 4'hE, OP_STR = 4'hF;

  // immediate group (op 0), selected by hi
  localparam I_LDI = 4'h0, I_ADI = 4'h1, I_SBI = 4'h2, I_ANI = 4'h3,
             I_ORI = 4'h4, I_XRI = 4'h5, I_CPI = 4'h6, I_LDX = 4'h7,
             I_CPX = 4'h8, I_ADC = 4'h9, I_SBC = 4'hA, I_LXH = 4'hB;

  // misc group (op D), selected by hi
  localparam M_OUT = 4'h0, M_IN  = 4'h1, M_SHL = 4'h2, M_SHR = 4'h3,
             M_NOT = 4'h4, M_HLT = 4'h5, M_TAX = 4'h6, M_TXA = 4'h7,
             M_INX = 4'h8, M_DEX = 4'h9, M_PSH = 4'hA, M_POP = 4'hB,
             M_RET = 4'hC, M_TXH = 4'hD, M_THA = 4'hE;  // TAXH, TXHA

  localparam S_FETCH = 3'd0,  // start 2-byte read at PC
             S_FWAIT = 3'd1,  // wait for instruction bytes
             S_EXEC  = 3'd2,  // execute / start memory access
             S_MWAIT = 3'd3,  // wait for data read or write
             S_HALT  = 3'd4,
             S_STRL  = 3'd5;  // STRLEN: stream bytes until NUL

  // ---------------- reset synchroniser ----------------
  // rst_n has no timing relation to clk. Without this, flip-flops could
  // leave reset on different clocks (seen in simulation: the first
  // instruction is skipped). Reset now starts and ends 2 clocks late, the
  // same for every flip-flop. These two flip-flops have no reset themselves.
  reg  [1:0]  rst_sync;
  always @(posedge clk) rst_sync <= {rst_sync[0], rst_n};
  wire        rst_ni = rst_sync[1];

  reg  [2:0]  state;
  reg  [11:0] pc;
  reg  [7:0]  a, sp;
  reg  [11:0] x;       // index register: a full address
  reg         zf, cf;
  reg  [7:0]  ir, arg;

  wire [3:0]  op = ir[7:4];
  wire [3:0]  hi = ir[3:0];
  wire [11:0] ea = {hi, arg};

  wire is_msc  = (op == OP_MSC);
  wire m_push  = is_msc && hi == M_PSH;
  wire m_pop   = is_msc && hi == M_POP;
  wire m_ret   = is_msc && hi == M_RET;
  wire is_call = (op == OP_CAL);
  wire indexed = (op == OP_LDX) || (op == OP_STX) || (op == OP_ADX);
  wire write   = (op == OP_STA) || (op == OP_STX) || m_push || is_call;

  // ---------------- address generation ----------------
  // One adder for SP: CALL -2, PUSH -1, POP +1, RET +2. Pushes pre-decrement.
  wire [7:0]  sp_new = sp + (is_call ? 8'hFE : m_push ? 8'hFF : m_ret ? 8'h02 : 8'h01);
  wire [11:0] maddr  = indexed            ? ea + x :
                       (m_push | is_call) ? {4'hF, sp_new} :
                       (m_pop  | m_ret)   ? {4'hF, sp} : ea;

  // Memory-class instructions start an SPI transaction in S_EXEC
  wire mem_op  = (op != OP_IMM && op != OP_MSC && op != OP_JMP && op != OP_JZ &&
                  op != OP_JC  && op != OP_JNZ) || m_push || m_pop || m_ret;

  // ---------------- transaction request (one shared start path) ----------------
  // CS must stay high for at least CS_GAP+1 clocks between transactions:
  // spi-ram-emu on the RP2040 needs ~50 of its clocks to re-arm after CS
  // rises (8 clocks = 800 ns at 10 MHz). A transaction that would start
  // earlier waits in its state.
  localparam CS_GAP = 3'd7;
  reg  [2:0]  gap;
  wire        t_want  = (state == S_FETCH) || (state == S_EXEC && mem_op);
  wire        stall   = t_want && (gap != 3'd0);
  wire        t_fetch = (state == S_FETCH);
  wire        t_go    = t_want && !stall;
  wire        t_wr    = !t_fetch && write;
  wire        t_two   = t_fetch || is_call || m_ret;        // 2 data bytes
  wire [11:0] t_addr  = t_fetch ? pc : maddr;
  wire [15:0] t_data  = is_call ? {4'h0, pc} : {a, 8'h00};  // pc is already PC+2

  // ---------------- SPI shifter ----------------
  reg  [39:0] tx;      // cmd(8) addr(16) data(16), shifted out MSB first
  reg  [15:0] rx;      // last 16 bits received
  reg  [5:0]  bitcnt;
  reg  [5:0]  lastbit; // index of last bit in this transaction
  reg         busy;

  assign spi_mosi = tx[39];
  assign halted   = (state == S_HALT);

  // ---------------- ALU ----------------
  // fn: 0 pass, 1 add, 2 sub, 3 and, 4 or, 5 xor
  reg [2:0] fn;
  always @* begin
    case (op)
      OP_IMM: case (hi)
        I_ADI, I_ADC: fn = 3'd1;
        I_SBI, I_CPI, I_CPX, I_SBC: fn = 3'd2;
        I_ANI:        fn = 3'd3;
        I_ORI:        fn = 3'd4;
        I_XRI:        fn = 3'd5;
        default:      fn = 3'd0;           // LDI, LDX
      endcase
      OP_ADD, OP_ADX: fn = 3'd1;
      OP_SUB, OP_CMP: fn = 3'd2;
      default:        fn = 3'd0;           // LDA, LDA ea,X, POP
    endcase
  end

  wire [7:0] lhs = (op == OP_IMM && hi == I_CPX) ? x[7:0] : a;
  wire       xh0 = (x[11:8] == 4'd0);  // CPX compares all of X with the 8-bit immediate
  wire [7:0] b   = (state == S_MWAIT) ? rx[7:0] : arg;
  // One adder for add, subtract and compare, with a carry input:
  //   add: lhs + b + cin          subtract: lhs + ~b + !cin  (= lhs - b - cin)
  // ADC / SBC take cin from the carry flag (carry in / borrow in), the
  // others use 0. The low bit of each operand only feeds the carry in.
  wire       cin    = cf && (op == OP_IMM) && (hi == I_ADC || hi == I_SBC);
  wire       sub    = (fn == 3'd2);
  wire [9:0] sum    = {1'b0, lhs, 1'b1} + {1'b0, b ^ {8{sub}}, sub ^ cin};
  wire [8:0] addsub = {sum[9] ^ sub, sum[8:1]};  // {carry or borrow, result}
  reg  [8:0] alu;  // {carry, result}
  always @* begin
    case (fn)
      3'd1, 3'd2: alu = addsub;
      3'd3:    alu = {cf, lhs & b};
      3'd4:    alu = {cf, lhs | b};
      3'd5:    alu = {cf, lhs ^ b};
      default: alu = {cf, b};
    endcase
  end
  wire alu_z = (alu[7:0] == 8'd0);

  wire [11:0] x_step = x + ((hi == M_DEX) ? 12'hFFF : 12'h001);  // INX / DEX

  always @(posedge clk) begin
    if (!rst_ni) begin
      state    <= S_FETCH;
      pc       <= 12'd0;
      a        <= 8'd0;
      x        <= 12'd0;
      sp       <= 8'd0;
      zf       <= 1'b0;
      cf       <= 1'b0;
      ir       <= 8'd0;
      arg      <= 8'd0;
      out_port <= 8'd0;
      out_strobe <= 1'b0;
      spi_cs_n <= 1'b1;
      spi_sck  <= 1'b0;
      tx       <= 40'd0;
      gap      <= 3'd0;
      rx       <= 16'd0;
      bitcnt   <= 6'd0;
      lastbit  <= 6'd0;
      busy     <= 1'b0;
    end else begin
      // SPI mode 0: MOSI changes while SCK low. MISO is sampled
      //   miso_rise = 0: at the end of the SCK high phase. For a RAM that
      //     changes MISO after the falling edge (23LC512, rp2040/ firmware);
      //     gives it almost a full SCK period.
      //   miso_rise = 1: at the clock edge that raises SCK, like a standard
      //     mode-0 master. Needed for a RAM that changes MISO right after
      //     the rising edge (stock spi-ram-emu); also works with the others,
      //     with half an SCK period of setup.
      if (busy) begin
        if (!spi_sck) begin
          spi_sck <= 1'b1;
          if (miso_rise) rx <= {rx[14:0], spi_miso};
        end else begin
          spi_sck <= 1'b0;
          if (!miso_rise) rx <= {rx[14:0], spi_miso};
          tx      <= {tx[38:0], 1'b0};
          bitcnt  <= bitcnt + 6'd1;
          if (bitcnt == lastbit) begin
            busy <= 1'b0;
            // STRLEN keeps CS low so the RAM keeps streaming sequential bytes
            if (state != S_STRL) spi_cs_n <= 1'b1;
            if (state != S_STRL) gap <= CS_GAP;
          end
        end
      end else if (gap != 3'd0) begin
        gap <= gap - 3'd1;
      end

      if (t_go) begin
        tx       <= {7'b0000001, !t_wr, 4'h0, t_addr, t_data};  // 0x03 READ / 0x02 WRITE
        lastbit  <= t_two ? 6'd39 : 6'd31;  // 24 header bits + 2 or 1 data bytes
        bitcnt   <= 6'd0;
        busy     <= 1'b1;
        spi_cs_n <= 1'b0;
      end

      if (!stall) case (state)
        S_FETCH: state <= S_FWAIT;

        S_FWAIT: if (!busy) begin
          ir    <= rx[15:8];
          arg   <= rx[7:0];
          pc    <= pc + 12'd2;
          state <= S_EXEC;
        end

        S_EXEC: begin
          state <= S_FETCH;
          case (op)
            OP_IMM: case (hi)
              I_LDI, I_ADI, I_SBI, I_ANI, I_ORI, I_XRI, I_ADC, I_SBC: begin
                a  <= alu[7:0];
                cf <= alu[8];
                zf <= alu_z;
              end
              I_CPI: begin
                cf <= alu[8];
                zf <= alu_z;
              end
              I_CPX: begin  // X >= 256 is above any immediate: no borrow, not equal
                cf <= alu[8] & xh0;
                zf <= alu_z  & xh0;
              end
              I_LDX: begin
                x  <= {4'h0, arg};
                zf <= (arg == 8'd0);
              end
              I_LXH: x[11:8] <= arg[3:0];  // LDXH: flags kept
              default: ;  // reserved: NOP
            endcase
            OP_LDA, OP_STA, OP_ADD, OP_SUB, OP_CMP, OP_LDX, OP_STX, OP_ADX,
            OP_CAL: begin
              state <= S_MWAIT;
              if (is_call) begin  // return address is written as 2 bytes
                sp <= sp_new;
                pc <= ea;
              end
            end
            OP_STR: begin  // STRLEN ea: one sequential read, count until NUL
              a        <= 8'd0;
              state    <= S_STRL;
            end
            OP_JMP: pc <= ea;
            OP_JZ:  if (zf)  pc <= ea;
            OP_JC:  if (cf)  pc <= ea;
            OP_JNZ: if (!zf) pc <= ea;
            OP_MSC: case (hi)
              M_OUT: begin out_port <= a; out_strobe <= ~out_strobe; end
              M_IN:  begin a <= in_port; zf <= (in_port == 8'd0); end
              M_SHL: begin {cf, a} <= {a, 1'b0};  zf <= (a[6:0] == 7'd0); end
              M_SHR: begin {a, cf} <= {1'b0, a};  zf <= (a[7:1] == 7'd0); end
              M_NOT: begin a <= ~a; zf <= (a == 8'hFF); end
              M_HLT: state <= S_HALT;
              M_TAX: begin x <= {4'h0, a}; zf <= (a == 8'd0); end
              M_TXA: begin a <= x[7:0]; zf <= (x[7:0] == 8'd0); end
              M_INX, M_DEX: begin x <= x_step; zf <= (x_step == 12'd0); end
              M_TXH: x[11:8] <= a[3:0];  // TAXH: flags kept
              M_THA: begin a <= {4'h0, x[11:8]}; zf <= xh0; end  // TXHA
              M_PSH, M_POP, M_RET: begin
                state <= S_MWAIT;
                sp    <= sp_new;
              end
              default: ;  // NOP
            endcase
            default: ;
          endcase
        end

        S_MWAIT: if (!busy) begin
          if (m_ret) begin
            pc <= rx[11:0];
          end else if (!write) begin  // loads, ALU ops, CMP, POP
            if (op != OP_CMP) a <= alu[7:0];
            cf <= alu[8];
            zf <= alu_z;
          end
          state <= S_FETCH;
        end

        S_STRL: if (!busy) begin
          if (rx[7:0] == 8'd0 || a == 8'hFF) begin
            spi_cs_n <= 1'b1;
            gap      <= CS_GAP;
            zf       <= (a == 8'd0);
            cf       <= (rx[7:0] != 8'd0);  // no NUL within 255 bytes
            state    <= S_FETCH;
          end else begin
            a      <= a + 8'd1;
            bitcnt <= 6'd24;  // clock in one more data byte
            busy   <= 1'b1;
          end
        end

        default: ;  // S_HALT: stay until reset
      endcase
    end
  end

endmodule
