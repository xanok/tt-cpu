`default_nettype none
`timescale 1ns / 1ps

/* This testbench just instantiates the module and makes some convenient wires
   that can be driven / tested by the cocotb test.py.
*/
module tb ();

  // Dump the signals to a FST file. You can view it with gtkwave or surfer.
  initial begin
    $dumpfile("tb.fst");
`ifdef GL_TEST
    $dumpvars(1, tb);  // pins only: every net of the netlist would be huge and slow
`else
    $dumpvars(0, tb);
`endif
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
  wire       halted_pin = uio_out[4];
  wire       strobe_pin = uio_out[5];  // toggles on every OUT
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
  // With -DMISO_RISE (make MISO_RISE=yes) the RAM changes MISO after the
  // rising SCK edge, like the stock spi-ram-emu, and the MISO_RISE strap
  // (uio[6]) is set so the CPU samples on the rising edge.
`ifdef MISO_RISE
  localparam MISO_RISE_STRAP = 1'b1;
`else
  localparam MISO_RISE_STRAP = 1'b0;
`endif
  assign uio_in = {1'b0, MISO_RISE_STRAP, 3'b0, ram_miso, 2'b0};
  spi_ram_model #(.AW(12), .EARLY(MISO_RISE_STRAP)) ram (
      .clk (clk),
      .cs_n(uio_out[0]),
      .sck (uio_out[3]),
      .mosi(uio_out[1]),
      .in_valid(1'b0), .in_data(8'h00), .in_clr(1'b0), .in_take(),
      .miso(ram_miso)
  );

  // For test_programs.py: the number of clocks from reset until HLT.
  reg  [31:0] cycles;
  always @(posedge clk)
    if (!rst_n) cycles <= 32'd0;
    else if (!uio_out[4]) cycles <= cycles + 32'd1;

endmodule
