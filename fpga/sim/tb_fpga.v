/*
 * Simulation of the Tang Nano 20K top (run from fpga/: make sim PROG=...).
 * Runs the program, decodes the UART line like the PC's serial port and
 * writes the received bytes to build/uart.txt. Then presses S1 (reset) and
 * lets the program run a second time, so the output appears twice.
 */
`default_nettype none
`timescale 1ns / 1ps

module tb_fpga;
  reg clk = 1'b0;
  always #18.518 clk = ~clk;  // 27 MHz

  reg btn_s1 = 1'b0, btn_s2 = 1'b0;
  wire [5:0] led;
  wire uart_tx;

  tangnano20k_top dut (
      .clk(clk), .btn_s1(btn_s1), .btn_s2(btn_s2), .led(led), .uart_tx(uart_tx)
  );

  // UART receiver: 115200 baud, sample in the middle of each bit
  localparam real BIT_NS = 1e9 / 115200.0;
  integer f, i, nbytes = 0;
  reg [7:0] byte_rx;
  initial begin
    f = $fopen("build/uart.txt", "wb");
    forever begin
      @(negedge uart_tx);                     // start bit
      #(BIT_NS * 1.5);
      for (i = 0; i < 8; i = i + 1) begin
        byte_rx[i] = uart_tx;
        #(BIT_NS);
      end
      if (uart_tx !== 1'b1) begin
        $display("tb_fpga: framing error (no stop bit)");
        $finish;
      end
      $fwrite(f, "%c", byte_rx);
      nbytes = nbytes + 1;
    end
  end

  wire halted = dut.uio_out[4];
  wire idle   = dut.uart.empty && dut.uart.bits == 4'd0 && !dut.uart.loading;

  task wait_done;
    begin
      wait (halted);
      wait (idle);
      #(BIT_NS * 12);                         // let the receiver finish the last byte
    end
  endtask

  initial begin
    wait_done;
    $display("tb_fpga: run 1 done at %0t ns, %0d bytes", $time, nbytes);
    btn_s1 = 1'b1;                            // press reset
    #(1000);
    btn_s1 = 1'b0;
    wait (!halted);
    wait_done;
    $display("tb_fpga: run 2 done at %0t ns, %0d bytes", $time, nbytes);
    $fclose(f);
    $finish;
  end

  initial begin
    #500_000_000;                             // 0.5 s simulated
    $display("tb_fpga: TIMEOUT");
    $finish;
  end
endmodule
