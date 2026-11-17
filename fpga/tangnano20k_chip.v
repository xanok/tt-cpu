/*
 * Tang Nano 20K as a stand-in for the cpu8 CHIP ONLY (bench test, see
 * docs/bench.md). Unlike tangnano20k.v there is no RAM inside the FPGA:
 * the chip's pins go to the header pins, and a Raspberry Pi Pico 2 running
 * the demo board firmware (rp2040/, -DTT_BOARD=pico2) provides the clock,
 * reset and the SPI RAM, exactly as the Tiny Tapeout demo board will.
 *
 *   LEDs   uo_out[5:0] (last OUT value)
 *   clk    from the Pico, not the on-board 27 MHz oscillator
 */
`default_nettype none

module tangnano20k_chip (
    input  wire       clk,       // chip clock from the Pico (10 MHz)
    input  wire       rst_n,
    input  wire [2:0] ui_in,     // ui_in[7:3] read 0 (not enough safe header pins)
    output wire [7:0] uo_out,
    output wire       spi_cs_n,  // uio[0]
    output wire       spi_mosi,  // uio[1]
    input  wire       spi_miso,  // uio[2]
    output wire       spi_sck,   // uio[3]
    output wire       halted,    // uio[4]
    output wire       out_strobe,// uio[5]
    input  wire       miso_rise, // uio[6], strap
    output wire [5:0] led        // active low
);
  wire [7:0] uio_out, uio_oe;

  tt_um_xanok_cpu8 dut (
      .ui_in  ({5'b0, ui_in}),
      .uo_out (uo_out),
      .uio_in ({1'b0, miso_rise, 3'b0, spi_miso, 2'b0}),
      .uio_out(uio_out),
      .uio_oe (uio_oe),
      .ena    (1'b1),
      .clk    (clk),
      .rst_n  (rst_n)
  );

  assign spi_cs_n   = uio_out[0];
  assign spi_mosi   = uio_out[1];
  assign spi_sck    = uio_out[3];
  assign halted     = uio_out[4];
  assign out_strobe = uio_out[5];
  assign led        = ~uo_out[5:0];
endmodule
