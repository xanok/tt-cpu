/*
 * cpu8 - tiny 8-bit accumulator CPU with an SPI memory bus
 * SPDX-License-Identifier: Apache-2.0
 *
 * - One accumulator A, flags Z and C, 12-bit PC (4 KB address space).
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
    input  wire       rst_n,
    input  wire [7:0] in_port,
    output reg  [7:0] out_port,
    output wire       halted,
    output reg        spi_cs_n,
    output reg        spi_sck,
    output wire       spi_mosi,
    input  wire       spi_miso
);

  localparam OP_LDI = 4'h0, OP_LDA = 4'h1, OP_STA = 4'h2, OP_ADD = 4'h3,
             OP_SUB = 4'h4, OP_AND = 4'h5, OP_OR  = 4'h6, OP_XOR = 4'h7,
             OP_ADI = 4'h8, OP_JMP = 4'h9, OP_JZ  = 4'hA, OP_JC  = 4'hB,
             OP_JNZ = 4'hC, OP_MSC = 4'hD, OP_CMP = 4'hE, OP_STR = 4'hF;

  localparam S_FETCH = 3'd0,  // start 2-byte read at PC
             S_FWAIT = 3'd1,  // wait for instruction bytes
             S_EXEC  = 3'd2,  // execute / start memory access
             S_MWAIT = 3'd3,  // wait for data read or write
             S_HALT  = 3'd4,
             S_STRL  = 3'd5;  // STRLEN: stream bytes until NUL

  reg  [2:0]  state;
  reg  [11:0] pc;
  reg  [7:0]  a;
  reg         zf, cf;
  reg  [7:0]  ir, arg;

  wire [3:0]  op = ir[7:4];
  wire [11:0] ea = {ir[3:0], arg};

  // ---------------- SPI shifter ----------------
  reg  [31:0] tx;      // cmd(8) addr(16) wdata(8), shifted out MSB first
  reg  [15:0] rx;      // last 16 bits received
  reg  [5:0]  bitcnt;
  reg  [5:0]  lastbit; // index of last bit in this transaction
  reg         busy;

  assign spi_mosi = tx[31];
  assign halted   = (state == S_HALT);

  // ---------------- ALU ----------------
  wire [7:0] b = (state == S_MWAIT) ? rx[7:0] : arg;
  reg  [8:0] alu;  // {carry, result}
  always @* begin
    case (op)
      OP_ADD, OP_ADI: alu = {1'b0, a} + {1'b0, b};
      OP_SUB, OP_CMP: alu = {1'b0, a} - {1'b0, b};  // carry = borrow
      OP_AND:         alu = {cf, a & b};
      OP_OR:          alu = {cf, a | b};
      OP_XOR:         alu = {cf, a ^ b};
      default:        alu = {cf, b};                // LDI, LDA
    endcase
  end

  always @(posedge clk) begin
    if (!rst_n) begin
      state    <= S_FETCH;
      pc       <= 12'd0;
      a        <= 8'd0;
      zf       <= 1'b0;
      cf       <= 1'b0;
      ir       <= 8'd0;
      arg      <= 8'd0;
      out_port <= 8'd0;
      spi_cs_n <= 1'b1;
      spi_sck  <= 1'b0;
      tx       <= 32'd0;
      rx       <= 16'd0;
      bitcnt   <= 6'd0;
      lastbit  <= 6'd0;
      busy     <= 1'b0;
    end else begin
      // SPI mode 0: MOSI changes while SCK low, MISO sampled at the end of
      // the high phase (late sampling tolerates pad/board delays).
      if (busy) begin
        if (!spi_sck) begin
          spi_sck <= 1'b1;
        end else begin
          spi_sck <= 1'b0;
          rx      <= {rx[14:0], spi_miso};
          tx      <= {tx[30:0], 1'b0};
          bitcnt  <= bitcnt + 6'd1;
          if (bitcnt == lastbit) begin
            busy <= 1'b0;
            // STRLEN keeps CS low so the RAM keeps streaming sequential bytes
            if (state != S_STRL) spi_cs_n <= 1'b1;
          end
        end
      end

      case (state)
        S_FETCH: begin
          tx       <= {8'h03, 4'h0, pc, 8'h00};
          lastbit  <= 6'd39;  // 24 header bits + 2 data bytes
          bitcnt   <= 6'd0;
          busy     <= 1'b1;
          spi_cs_n <= 1'b0;
          state    <= S_FWAIT;
        end

        S_FWAIT: if (!busy) begin
          ir    <= rx[15:8];
          arg   <= rx[7:0];
          pc    <= pc + 12'd2;
          state <= S_EXEC;
        end

        S_EXEC: begin
          state <= S_FETCH;
          case (op)
            OP_LDI, OP_ADI: begin
              a  <= alu[7:0];
              cf <= alu[8];
              zf <= (alu[7:0] == 8'd0);
            end
            OP_LDA, OP_ADD, OP_SUB, OP_AND, OP_OR, OP_XOR, OP_CMP,
            OP_STA: begin
              tx       <= {(op == OP_STA) ? 8'h02 : 8'h03, 4'h0, ea, a};
              lastbit  <= 6'd31;
              bitcnt   <= 6'd0;
              busy     <= 1'b1;
              spi_cs_n <= 1'b0;
              state    <= S_MWAIT;
            end
            OP_STR: begin  // STRLEN ea: one sequential read, count until NUL
              tx       <= {8'h03, 4'h0, ea, 8'h00};
              lastbit  <= 6'd31;
              bitcnt   <= 6'd0;
              busy     <= 1'b1;
              spi_cs_n <= 1'b0;
              a        <= 8'd0;
              state    <= S_STRL;
            end
            OP_JMP: pc <= ea;
            OP_JZ:  if (zf)  pc <= ea;
            OP_JC:  if (cf)  pc <= ea;
            OP_JNZ: if (!zf) pc <= ea;
            OP_MSC: case (ir[3:0])
              4'h0: out_port <= a;                                    // OUT
              4'h1: begin a <= in_port; zf <= (in_port == 8'd0); end  // IN
              4'h2: begin {cf, a} <= {a, 1'b0};  zf <= (a[6:0] == 7'd0); end // SHL
              4'h3: begin {a, cf} <= {1'b0, a};  zf <= (a[7:1] == 7'd0); end // SHR
              4'h4: begin a <= ~a; zf <= (a == 8'hFF); end            // NOT
              4'h5: state <= S_HALT;                                  // HLT
              default: ;                                              // NOP
            endcase
            default: ;
          endcase
        end

        S_MWAIT: if (!busy) begin
          if (op != OP_STA) begin
            if (op != OP_CMP) a <= alu[7:0];
            cf <= alu[8];
            zf <= (alu[7:0] == 8'd0);
          end
          state <= S_FETCH;
        end

        S_STRL: if (!busy) begin
          if (rx[7:0] == 8'd0 || a == 8'hFF) begin
            spi_cs_n <= 1'b1;
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
