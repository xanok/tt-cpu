/*
 * FPGA-only: receive bytes from the Tang Nano 20K's USB-serial chip (BL616),
 * 8N1, LSB first, and keep them in a small FIFO until the input mailbox in
 * the RAM (see spi_ram_model, MBOX) takes them. This is the FPGA's stand-in
 * for the RP2040 firmware's PUT queue on the TT demo board.
 * When the FIFO is full, new bytes are dropped.
 */
`default_nettype none

module uart_in #(
    parameter CLK_HZ = 27_000_000,
    parameter BAUD   = 115_200,
    parameter AW     = 6            // FIFO depth 2^AW bytes
) (
    input  wire       clk,
    input  wire       rst_n,        // CPU reset: empties the FIFO
    input  wire       rx,
    output wire       valid,        // a byte is waiting
    output wire [7:0] data,
    input  wire       take          // the waiting byte was used
);
  localparam DIV = (CLK_HZ + BAUD / 2) / BAUD;  // clocks per bit

  // ---- receiver ----
  reg [1:0]  rx_sync = 2'b11;
  reg        busy    = 1'b0;
  reg [15:0] cnt     = 16'd0;
  reg [3:0]  nbit    = 4'd0;     // 0 = start bit, 1..8 = data, 9 = stop
  reg [7:0]  sh      = 8'd0;
  reg        got     = 1'b0;     // one clock: sh holds a new byte
  wire       rxs     = rx_sync[1];

  always @(posedge clk) begin
    rx_sync <= {rx_sync[0], rx};
    got     <= 1'b0;
    if (!busy) begin
      if (!rxs) begin              // start bit begins
        busy <= 1'b1;
        cnt  <= DIV / 2;           // sample in the middle of each bit
        nbit <= 4'd0;
      end
    end else if (cnt != 16'd0) begin
      cnt <= cnt - 16'd1;
    end else begin
      cnt  <= DIV - 1;
      nbit <= nbit + 4'd1;
      if (nbit == 4'd0) begin
        if (rxs) busy <= 1'b0;     // glitch, not a start bit
      end else if (nbit == 4'd9) begin
        busy <= 1'b0;
        got  <= rxs;               // stop bit must be high
      end else begin
        sh <= {rxs, sh[7:1]};
      end
    end
  end

  // ---- FIFO ----
  reg [7:0]  fifo [0:(1<<AW)-1];
  reg [AW:0] wr = 0, rd = 0;
  wire       full = (wr - rd) == (1 << AW);
  assign valid = (wr != rd);
  assign data  = fifo[rd[AW-1:0]];

  always @(posedge clk) begin
    if (got && !full && rst_n) fifo[wr[AW-1:0]] <= sh;
    if (!rst_n) begin
      wr <= 0;
      rd <= 0;
    end else begin
      if (got && !full)  wr <= wr + 1'b1;
      if (take && valid) rd <= rd + 1'b1;
    end
  end
endmodule
