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
 */

`default_nettype none

module spi_ram_model #(
    parameter AW    = 12,
    parameter INIT  = "",
    parameter EARLY = 0
) (
    input  wire clk,
    input  wire cs_n,
    input  wire sck,
    input  wire mosi,
    output reg  miso
);

  reg [7:0] mem [0:(1<<AW)-1];
  initial if (INIT != "") $readmemh(INIT, mem);

  generate if (EARLY == 0) begin : g_late

    reg        sck_q = 1'b0;
    reg [5:0]  cnt   = 6'd0;   // bit index; wraps 31 -> 24 for each data byte
    reg [7:0]  cmd   = 8'd0;
    reg [15:0] addr  = 16'd0;
    reg [7:0]  sh    = 8'd0;
    reg [7:0]  obuf  = 8'd0;
    reg [7:0]  rd    = 8'd0;
    initial miso = 1'b0;

    wire rise = sck & ~sck_q;
    wire fall = ~sck & sck_q;

    // Synchronous read port. When the last address bit arrives, look up the
    // fully assembled address so the first data bit is ready on the next fall.
    wire [AW-1:0] ra = (rise && cnt == 6'd23) ? {addr[AW-2:0], mosi} : addr[AW-1:0];
    always @(posedge clk) rd <= mem[ra];

    always @(posedge clk) begin
      sck_q <= sck;
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
