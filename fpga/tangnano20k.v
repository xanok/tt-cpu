/*
 * Tang Nano 20K top: tt_um_xanok_cpu8 + on-FPGA SPI RAM (block RAM preloaded with
 * the assembled program). The CPU is the exact same RTL that goes to the chip.
 */
`default_nettype none

module tangnano20k_top (
    input  wire       clk,     // 27 MHz
    input  wire       btn_s1,  // -> ui_in[0]
    input  wire       btn_s2,  // -> ui_in[1]
    output wire [5:0] led      // active low, shows uo_out[5:0]
);
  // Power-on reset
  reg [7:0] por = 8'd0;
  always @(posedge clk) if (!por[7]) por <= por + 8'd1;
  wire rst_n = por[7];

  wire [7:0] uo_out, uio_out, uio_oe;
  wire       miso;

  tt_um_xanok_cpu8 dut (
      .ui_in  ({6'b0, btn_s2, btn_s1}),
      .uo_out (uo_out),
      .uio_in ({5'b0, miso, 2'b0}),
      .uio_out(uio_out),
      .uio_oe (uio_oe),
      .ena    (1'b1),
      .clk    (clk),
      .rst_n  (rst_n)
  );

  spi_ram_model #(.AW(12), .INIT("build/prog.hex")) ram (
      .clk (clk),
      .cs_n(uio_out[0]),
      .sck (uio_out[3]),
      .mosi(uio_out[1]),
      .miso(miso)
  );

  assign led = ~uo_out[5:0];
endmodule
