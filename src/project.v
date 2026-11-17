/*
 * Tiny Tapeout wrapper for cpu8
 * SPDX-License-Identifier: Apache-2.0
 */

`default_nettype none

// Silicon art: UNISC grid icon on met4 (macros/unisc_logo.*). No logic, no pins.
(* blackbox *) (* keep *)
module unisc_logo ();
endmodule

module tt_um_xanok_cpu8 (
    input  wire [7:0] ui_in,    // input port (IN instruction)
    output wire [7:0] uo_out,   // output port (OUT instruction)
    input  wire [7:0] uio_in,   // IOs: Input path
    output wire [7:0] uio_out,  // IOs: Output path
    output wire [7:0] uio_oe,   // IOs: Enable path (active high: 0=input, 1=output)
    input  wire       ena,      // always 1 when the design is powered, so you can ignore it
    input  wire       clk,      // clock
    input  wire       rst_n     // reset_n - low to reset
);

  wire cs_n, sck, mosi, halted, strobe;

  cpu8 core (
      .clk     (clk),
      .rst_n   (rst_n),
      .in_port (ui_in),
      .out_port(uo_out),
      .halted  (halted),
      .out_strobe(strobe),
      .spi_cs_n(cs_n),
      .spi_sck (sck),
      .spi_mosi(mosi),
      .spi_miso(uio_in[2])
  );

  // uio[0]=CS_N  uio[1]=MOSI  uio[2]=MISO(in)  uio[3]=SCK  uio[4]=HALTED
  // uio[5]=OUT_STROBE: toggles on every OUT, so the host can capture each
  // byte written to uo_out (also repeated values) - used for text output.
  assign uio_out = {2'b00, strobe, halted, sck, 1'b0, mosi, cs_n};
  assign uio_oe  = 8'b0011_1011;

  wire _unused = &{ena, uio_in[7:3], uio_in[1:0], 1'b0};

`ifndef SYNTH_FPGA
  (* keep *)
  unisc_logo logo ();
`endif

endmodule
