/*
 * Tang Nano 20K top: tt_um_xanok_cpu8 + on-FPGA SPI RAM (block RAM preloaded with
 * the assembled program). The CPU is the exact same RTL that goes to the chip.
 *
 *   LEDs    uo_out[5:0] (last OUT value)
 *   S1      reset: press to restart the program
 *   S2      ui_in[1] (ui_in[0] reads 0)
 *   UART    every OUT byte, 115200 8N1, over the board's USB-C cable
 *           (the second serial port, e.g. /dev/ttyUSB1); bytes sent by
 *           the PC go to the input mailbox (programs/lib/input.asm)
 */
`default_nettype none

module tangnano20k_top (
    input  wire       clk,     // 27 MHz
    input  wire       btn_s1,  // reset (high when pressed)
    input  wire       btn_s2,  // -> ui_in[1]
    output wire [5:0] led,     // active low, shows uo_out[5:0]
    output wire       uart_tx, // OUT bytes to the PC
    input  wire       uart_rx  // keyboard input from the PC
);
  // Power-on reset, then S1 resets the CPU (synchronised)
  reg [7:0] por = 8'd0;
  reg [1:0] s1_sync = 2'b00;
  always @(posedge clk) begin
    if (!por[7]) por <= por + 8'd1;
    s1_sync <= {s1_sync[0], btn_s1};
  end
  wire rst_n = por[7] && !s1_sync[1];

  wire [7:0] uo_out, uio_out, uio_oe;
  wire       miso;

  tt_um_xanok_cpu8 dut (
      .ui_in  ({6'b0, btn_s2, 1'b0}),
      .uo_out (uo_out),
      .uio_in ({5'b0, miso, 2'b0}),
      .uio_out(uio_out),
      .uio_oe (uio_oe),
      .ena    (1'b1),
      .clk    (clk),
      .rst_n  (rst_n)
  );

  wire       in_valid, in_take;
  wire [7:0] in_data;

  uart_in #(.CLK_HZ(27_000_000), .BAUD(115_200)) keyboard (
      .clk  (clk),
      .rst_n(rst_n),
      .rx   (uart_rx),
      .valid(in_valid),
      .data (in_data),
      .take (in_take)
  );

  spi_ram_model #(.AW(12), .INIT("build/prog.hex"), .MBOX(1)) ram (
      .clk (clk),
      .cs_n(uio_out[0]),
      .sck (uio_out[3]),
      .mosi(uio_out[1]),
      .miso(miso),
      .in_valid(in_valid),
      .in_data (in_data),
      .in_clr  (!rst_n),
      .in_take (in_take)
  );

  assign led = ~uo_out[5:0];

  uart_out #(.CLK_HZ(27_000_000), .BAUD(115_200)) uart (
      .clk   (clk),
      .rst_n (rst_n),
      .strobe(uio_out[5]),
      .data  (uo_out),
      .tx    (uart_tx)
  );
endmodule
