/*
 * FPGA-only: send every byte the CPU writes with OUT to the Tang Nano 20K's
 * USB-serial chip (BL616), 8N1, LSB first. A new byte is signalled by an edge
 * on OUT_STROBE (uio_out[5]), exactly like the RP2040 capture on the TT demo
 * board. A FIFO absorbs bursts: OUT can come every 3.3 us at 27 MHz, but one
 * byte at 115200 baud takes 87 us. When the FIFO is full, bytes are dropped.
 */
`default_nettype none

module uart_out #(
    parameter CLK_HZ = 27_000_000,
    parameter BAUD   = 115_200,
    parameter AW     = 10           // FIFO depth 2^AW bytes
) (
    input  wire       clk,
    input  wire       rst_n,        // CPU reset: its strobe edge then is not a byte
    input  wire       strobe,       // toggles once per byte
    input  wire [7:0] data,         // valid when strobe changes
    output reg        tx = 1'b1
);
  localparam DIV = (CLK_HZ + BAUD / 2) / BAUD;  // clocks per bit

  // ---- strobe edge -> FIFO ----
  reg strobe_q = 1'b0;
  reg [7:0]  fifo [0:(1<<AW)-1];
  reg [AW:0] wr = 0, rd = 0;
  wire       empty = (wr == rd);
  wire       full  = (wr - rd) == (1 << AW);

  always @(posedge clk) begin
    strobe_q <= strobe;
    // Reset clears the CPU's strobe flip-flop, which can itself be an edge:
    // ignore edges while the CPU is in reset.
    if (rst_n && strobe != strobe_q && !full) begin
      fifo[wr[AW-1:0]] <= data;
      wr <= wr + 1'b1;
    end
  end

  // ---- transmitter ----
  reg [10:0] shift = 11'h7FF;  // idle, stop, data, start (LSB first); the
                               // extra idle bit makes the stop bit a full bit long
  reg [3:0]  bits  = 4'd0;     // bits left to send
  reg [15:0] cnt   = 16'd0;
  reg [7:0]  head  = 8'd0;     // FIFO read data (block RAM: one cycle late)
  reg        loading = 1'b0;

  always @(posedge clk) head <= fifo[rd[AW-1:0]];

  always @(posedge clk) begin
    if (bits != 4'd0) begin
      if (cnt == DIV - 1) begin
        cnt   <= 16'd0;
        tx    <= shift[0];
        shift <= {1'b1, shift[10:1]};
        bits  <= bits - 4'd1;
      end else begin
        cnt <= cnt + 16'd1;
      end
    end else if (loading) begin       // head is valid now
      loading <= 1'b0;
      shift   <= {2'b11, head, 1'b0};
      bits    <= 4'd11;
      cnt     <= DIV - 1;             // start bit goes out on the next clock
      rd      <= rd + 1'b1;
    end else if (!empty) begin
      loading <= 1'b1;
    end
  end
endmodule
