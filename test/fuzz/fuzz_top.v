/*
 * Top for the Verilator random-program test (fuzz.cpp): the chip wrapper
 * plus the SPI RAM model. With -DMISO_RISE the strap is high and the RAM
 * has stock spi-ram-emu timing, like "make MISO_RISE=yes" in test/.
 */
`default_nettype none
`timescale 1ns / 1ps

module fuzz_top (
    input  wire       clk,
    input  wire       rst_n,
    input  wire [7:0] ui_in,
    output wire [7:0] uo_out,
    output wire       halted,
    output wire       strobe
);
`ifdef MISO_RISE
  localparam STRAP = 1'b1;
`else
  localparam STRAP = 1'b0;
`endif
  wire [7:0] uio_out, uio_oe;
  wire       miso;

  tt_um_xanok_cpu8 dut (
      .ui_in  (ui_in),
      .uo_out (uo_out),
      .uio_in ({1'b0, STRAP, 3'b0, miso, 2'b0}),
      .uio_out(uio_out),
      .uio_oe (uio_oe),
      .ena    (1'b1),
      .clk    (clk),
      .rst_n  (rst_n)
  );

  spi_ram_model #(.AW(12), .EARLY(STRAP)) ram (
      .clk (clk),
      .cs_n(uio_out[0]),
      .sck (uio_out[3]),
      .mosi(uio_out[1]),
      .miso(miso),
      .in_valid(1'b0), .in_data(8'h00), .in_clr(1'b0), .in_take()
  );

  assign halted = uio_out[4];
  assign strobe = uio_out[5];
  wire _unused = &{uio_oe, uio_out[7:6], uio_out[2], 1'b0};
endmodule
