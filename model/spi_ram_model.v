/*
 * Synchronous SPI RAM model (23LC512-style READ 0x03 / WRITE 0x02, mode 0,
 * 16-bit address, sequential mode), matching the TT demo board's RP2040
 * spi-ram-emu firmware. NOT part of the chip: used by the cocotb
 * testbench and as on-FPGA program memory (infers block RAM).
 *
 * Runs in the system clock domain and detects SCK edges, so SCK must be at
 * most clk/2 (which cpu8 guarantees).
 *
 * EARLY = 0: MISO changes after the falling SCK edge, like a real 23LC512
 *            and the modified spi-ram-emu in rp2040/. Infers block RAM.
 * EARLY = 1: MISO changes right after the rising SCK edge, like the stock
 *            spi-ram-emu (the master must sample on the rising edge: cpu8
 *            strap MISO_RISE = 1). Simulation only.
 *
 * MBOX = 1:  (FPGA build, EARLY = 0 only) the input mailbox of
 *            programs/lib/input.asm, which on the demo board is filled by
 *            the RP2040. Addresses 0xEFE (flag) and 0xEFF (data) are then
 *            two registers beside the RAM. When in_valid is high, the flag
 *            is 0 and no SPI transaction is running, in_data goes into the
 *            data register, the flag becomes 1 and in_take pulses. The CPU
 *            clears the flag by writing 0 to 0xEFE. in_clr clears the flag.
 */

`default_nettype none

module spi_ram_model #(
    parameter AW    = 12,
    parameter INIT  = "",
    parameter EARLY = 0,
    parameter MBOX  = 0
) (
    input  wire clk,
    input  wire cs_n,
    input  wire sck,
    input  wire mosi,
    output reg  miso,
    // input mailbox (MBOX = 1), tie in_valid and in_clr low otherwise
    input  wire       in_valid,
    input  wire [7:0] in_data,
    input  wire       in_clr,
    output wire       in_take
);

  localparam [15:0] MB_FLAG = 16'h0EFE, MB_DATA = 16'h0EFF;
  reg       mb_flag = 1'b0;
  reg [7:0] mb_data = 8'd0;
  assign in_take = (MBOX != 0) && in_valid && !mb_flag && cs_n && !in_clr;

  reg [7:0] mem [0:(1<<AW)-1];
  initial if (INIT != "") $readmemh(INIT, mem);

  generate if (EARLY == 0) begin : g_late

    reg        sck_q = 1'b0;
    reg [5:0]  cnt   = 6'd0;   // bit index; wraps 31 -> 24 for each data byte
    reg [7:0]  cmd   = 8'd0;
    reg [15:0] addr  = 16'd0;
    reg [7:0]  sh    = 8'd0;
    reg [7:0]  obuf  = 8'd0;
    wire [7:0] rd;
    initial miso = 1'b0;

    wire rise = sck & ~sck_q;
    wire fall = ~sck & sck_q;

    // Synchronous read port. When the last address bit arrives, look up the
    // fully assembled address so the first data bit is ready on the next fall.
    wire [AW-1:0] ra = (rise && cnt == 6'd23) ? {addr[AW-2:0], mosi} : addr[AW-1:0];
    reg  [AW-1:0] ra_q = 0;
    reg  [7:0]    rd_ram = 8'd0;
    always @(posedge clk) begin
      rd_ram <= mem[ra];
      ra_q   <= ra;
    end
    assign rd = (MBOX != 0 && ra_q == MB_DATA[AW-1:0]) ? mb_data :
                (MBOX != 0 && ra_q == MB_FLAG[AW-1:0]) ? {7'd0, mb_flag} : rd_ram;

    always @(posedge clk) begin
      sck_q <= sck;
      if (in_clr) mb_flag <= 1'b0;
      if (in_take) begin
        mb_data <= in_data;
        mb_flag <= 1'b1;
      end
      if (cs_n) begin
        cnt  <= 6'd0;
        miso <= 1'b0;
      end else if (rise) begin
        if (cnt < 6'd8)       cmd  <= {cmd[6:0], mosi};
        else if (cnt < 6'd24) addr <= {addr[14:0], mosi};
        else begin
          sh <= {sh[6:0], mosi};
          if (cnt == 6'd31 && cmd == 8'h02) begin
            mem[addr[AW-1:0]] <= {sh[6:0], mosi};
            if (MBOX != 0 && addr[AW-1:0] == MB_FLAG[AW-1:0]) mb_flag <= |{sh[6:0], mosi};
            if (MBOX != 0 && addr[AW-1:0] == MB_DATA[AW-1:0]) mb_data <= {sh[6:0], mosi};
            addr <= addr + 16'd1;
          end
        end
        cnt <= (cnt == 6'd31) ? 6'd24 : cnt + 6'd1;
      end else if (fall && cmd == 8'h03 && cnt >= 6'd24) begin
        if (cnt == 6'd24) begin  // start of a data byte
          miso <= rd[7];
          obuf <= {rd[6:0], 1'b0};
          addr <= addr + 16'd1;
        end else begin
          miso <= obuf[7];
          obuf <= {obuf[6:0], 1'b0};
        end
      end
    end

  end else begin : g_early

    reg        sck_q = 1'b0;
    integer    n     = 0;      // SCK rising edges since CS went low
    reg [7:0]  cmd   = 8'd0;
    reg [15:0] addr  = 16'd0;  // start address of the transaction
    reg [7:0]  sh    = 8'd0;
    reg [15:0] base;
    reg [7:0]  cur;
    integer    k;
    initial miso = 1'b0;

    wire rise = sck & ~sck_q;

    // The real emulator answers well within half an SCK period, so the new
    // bit is on MISO before SCK falls. This block therefore runs on the
    // falling clock edge: it sees an SCK rise half a clock after it happens
    // and answers at once. A CPU that samples at the end of the SCK high
    // phase then reads the wrong bit, as it would on hardware.
    always @(negedge clk) begin
      sck_q <= sck;
      if (cs_n) begin
        n    <= 0;
        miso <= 1'b0;
      end else if (rise) begin
        n <= n + 1;
        if (n < 8)       cmd  <= {cmd[6:0], mosi};
        else if (n < 24) addr <= {addr[14:0], mosi};
        else begin
          sh <= {sh[6:0], mosi};
          if (cmd == 8'h02 && (n - 24) % 8 == 7)
            mem[(addr + (n - 24) / 8) & ((1 << AW) - 1)] <= {sh[6:0], mosi};
        end
        // READ: after the last address bit and after every data bit, put
        // the next data bit on MISO (bit k of the stream, MSB first).
        if (cmd == 8'h03 && n >= 23) begin
          base = (n == 23) ? {addr[14:0], mosi} : addr;
          k    = n - 23;
          cur  = mem[(base + k / 8) & ((1 << AW) - 1)];
          miso <= cur[7 - (k % 8)];
        end
      end
    end

  end endgenerate

endmodule
