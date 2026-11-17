`default_nettype none
`timescale 1ns / 1ps

/* This testbench just instantiates the module and makes some convenient wires
   that can be driven / tested by the cocotb test.py.
*/
module tb ();

  // Dump the signals to a FST file. You can view it with gtkwave or surfer.
  initial begin
    $dumpfile("tb.fst");
    $dumpvars(0, tb);
    #1;
  end

  // Wire up the inputs and outputs:
  reg clk;
  reg rst_n;
  reg ena;
  reg [7:0] ui_in;
  wire [7:0] uio_in;
  wire ram_miso;
  wire [7:0] uo_out;
  wire [7:0] uio_out;
  wire [7:0] uio_oe;
`ifdef GL_TEST
  wire VPWR = 1'b1;
  wire VGND = 1'b0;
`endif

  tt_um_xanok_cpu8 user_project (

      // Include power ports for the Gate Level test:
`ifdef GL_TEST
      .VPWR(VPWR),
      .VGND(VGND),
`endif

      .ui_in  (ui_in),    // Dedicated inputs
      .uo_out (uo_out),   // Dedicated outputs
      .uio_in (uio_in),   // IOs: Input path
      .uio_out(uio_out),  // IOs: Output path
      .uio_oe (uio_oe),   // IOs: Enable path (active high: 0=input, 1=output)
      .ena    (ena),      // enable - goes high when design is selected
      .clk    (clk),      // clock
      .rst_n  (rst_n)     // not reset
  );

  // External SPI RAM (program + data). MISO goes to uio[2].
  assign uio_in = {5'b0, ram_miso, 2'b0};
  spi_ram_model #(.AW(12)) ram (
      .clk (clk),
      .cs_n(uio_out[0]),
      .sck (uio_out[3]),
      .mosi(uio_out[1]),
      .miso(ram_miso)
  );

`ifndef GL_TEST
  // Probes for test_programs.py (RTL only): pulse when an OUT instruction
  // executes, and the number of clocks from reset until HLT.
  wire       out_strobe = user_project.core.state == 3'd2 && user_project.core.ir == 8'hD0;
  wire [7:0] out_value  = user_project.core.a;
  wire       halted_pin = uio_out[4];
  reg  [31:0] cycles;
  always @(posedge clk)
    if (!rst_n) cycles <= 32'd0;
    else if (!uio_out[4]) cycles <= cycles + 32'd1;
`endif

endmodule
