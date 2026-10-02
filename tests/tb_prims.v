// Behavioural checks for the primitive library (run by ctest through iverilog).
`timescale 1ns/1ps
module tb;
  reg clk = 0, rst_n = 0;
  always #5 clk = ~clk;
  integer errors = 0, i, k, ones;

  // round-robin arbiter, 5 requesters
  reg [4:0] req = 0;
  wire [4:0] grant;
  rg_rr_arb #(.N(5)) arb (.clk(clk), .rst_n(rst_n), .req(req), .grant(grant));

  // FIFO, 8 bits x 4
  reg push = 0, pop = 0;
  reg [7:0] din = 0;
  wire [7:0] dout;
  wire full, empty;
  rg_fifo #(.WIDTH(8), .DEPTH(4)) fifo (.clk(clk), .rst_n(rst_n), .push(push), .din(din), .pop(pop),
                                       .dout(dout), .full(full), .empty(empty));

  // 3-stage pipeline and a 2-flop synchronizer
  reg [7:0] pd = 0;
  wire [7:0] pq;
  rg_pipe #(.WIDTH(8), .STAGES(3)) pipe (.clk(clk), .rst_n(rst_n), .d(pd), .q(pq));
  reg sd = 0;
  wire sq;
  rg_sync2 #(.WIDTH(1)) sync (.clk(clk), .rst_n(rst_n), .d(sd), .q(sq));

  task fail(input [255:0] what);
    begin
      errors = errors + 1;
      $display("FAIL at %0t: %0s", $time, what);
    end
  endtask

  initial begin
    #12 rst_n = 1;

    // everyone requesting: grants must rotate 0,1,2,3,4,0,1,...
    @(negedge clk) req = 5'b11111;
    for (i = 0; i < 12; i = i + 1) begin
      #1;
      if (grant !== (5'b1 << (i % 5))) fail("arbiter rotation");
      @(negedge clk);
    end
    // random requests: grant is one-hot (or zero) and only ever goes to a requester
    for (i = 0; i < 500; i = i + 1) begin
      req = $random;
      #1;
      ones = 0;
      for (k = 0; k < 5; k = k + 1) ones = ones + grant[k];
      if ((grant & ~req) != 0) fail("grant to a non-requester");
      if (ones > 1) fail("grant not one-hot");
      if (req != 0 && ones != 1) fail("requests but no grant");
      @(negedge clk);
    end
    req = 0;

    // FIFO: fill to full, then drain in order
    if (!empty || full) fail("fifo flags after reset");
    for (i = 0; i < 4; i = i + 1) begin
      push = 1;
      din = 8'hA0 + i;
      @(negedge clk);
    end
    push = 0;
    if (!full) fail("fifo not full after 4 pushes");
    for (i = 0; i < 4; i = i + 1) begin
      if (dout !== 8'hA0 + i) fail("fifo order");
      pop = 1;
      @(negedge clk);
    end
    pop = 0;
    if (!empty) fail("fifo not empty after draining");

    // pipeline: a value shows up exactly 3 cycles later
    pd = 8'h5A;
    @(negedge clk) pd = 8'h00;
    @(negedge clk) if (pq === 8'h5A) fail("pipe too early");
    @(negedge clk) if (pq !== 8'h5A) fail("pipe latency");

    // synchronizer: 2 cycles
    sd = 1;
    @(negedge clk) if (sq) fail("sync too early");
    @(negedge clk) if (!sq) fail("sync latency");

    if (errors == 0) $display("PASS");
    else $display("FAILED: %0d error(s)", errors);
    $finish;
  end
endmodule
