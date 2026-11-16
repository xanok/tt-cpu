/*
 * Synchronous SPI RAM model (23LC512-style READ 0x03 / WRITE 0x02, mode 0,
 * 16-bit address, sequential mode), matching the TT demo board's RP2040
 * spi-ram-emu firmware. NOT part of the chip: used by the cocotb
 * testbench and as on-FPGA program memory (infers block RAM).
 *
 * Runs in the system clock domain and detects SCK edges, so SCK must be at
 * most clk/2 (which cpu8 guarantees).
 */

`default_nettype none

module spi_ram_model #(
    parameter AW   = 12,
    parameter INIT = ""
) (
    input  wire clk,
    input  wire cs_n,
    input  wire sck,
    input  wire mosi,
    output reg  miso
);

  reg [7:0] mem [0:(1<<AW)-1];
  initial if (INIT != "") $readmemh(INIT, mem);

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

endmodule
