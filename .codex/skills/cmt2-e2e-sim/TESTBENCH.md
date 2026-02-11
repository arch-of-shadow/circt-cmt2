# Writing Testbenches for CMT2 Designs

## Basic Testbench Template

```systemverilog
`timescale 1ns/1ps

module testbench;
  // Clock and reset
  logic clk;
  logic reset;

  // Clock generation
  initial clk = 0;
  always #5 clk = ~clk;  // 100MHz clock

  // DUT instantiation
  // Replace with your module name and ports
  MyModule dut (
    .clock(clk),
    .reset(reset)
    // Add other ports here
  );

  // Test stimulus
  initial begin
    // Initialize
    reset = 1;

    // Wait for reset
    repeat(10) @(posedge clk);
    reset = 0;

    // Apply test vectors
    repeat(100) @(posedge clk);

    // Check results
    $display("Simulation complete");
    $finish;
  end

  // Optional: VCD dump
  initial begin
    $dumpfile("sim.vcd");
    $dumpvars(0, testbench);
  end
endmodule
```

## CMT2-Specific Considerations

### Clock and Reset Naming

CMT2/FIRRTL modules typically use:
- `clock` for clock signal
- `reset` for synchronous reset

### Method Invocation Pattern

For CMT2 methods compiled to RTL:
```systemverilog
// Enable method
dut.method_go = 1;
@(posedge clk);

// Wait for completion
while (!dut.method_done) @(posedge clk);

// Read result
result = dut.method_result;
```

### Rule Execution

Rules fire automatically when their guard is true:
```systemverilog
// Wait for rule to fire
while (!dut.rule_guard) @(posedge clk);
// Rule fires on next cycle
@(posedge clk);
```

## Example: GCD Testbench

```systemverilog
`timescale 1ns/1ps

module gcd_tb;
  logic clk, reset;
  logic [31:0] a_in, b_in;
  logic [31:0] result;
  logic start, done;

  initial clk = 0;
  always #5 clk = ~clk;

  GCD dut (
    .clock(clk),
    .reset(reset),
    .io_a(a_in),
    .io_b(b_in),
    .io_start(start),
    .io_done(done),
    .io_result(result)
  );

  initial begin
    reset = 1;
    start = 0;
    a_in = 0;
    b_in = 0;

    repeat(5) @(posedge clk);
    reset = 0;

    // Test: GCD(48, 18) = 6
    @(posedge clk);
    a_in = 48;
    b_in = 18;
    start = 1;
    @(posedge clk);
    start = 0;

    // Wait for completion
    while (!done) @(posedge clk);

    // Check result
    if (result == 6)
      $display("PASS: GCD(48,18) = %d", result);
    else
      $display("FAIL: GCD(48,18) = %d, expected 6", result);

    repeat(5) @(posedge clk);
    $finish;
  end

  initial begin
    $dumpfile("gcd.vcd");
    $dumpvars(0, gcd_tb);
  end
endmodule
```

## Assertion-Based Verification

```systemverilog
// Immediate assertion
always @(posedge clk) begin
  if (!reset) begin
    assert (dut.counter >= 0) else $error("Counter negative!");
  end
end

// Concurrent assertion
property done_after_start;
  @(posedge clk) disable iff (reset)
  start |-> ##[1:100] done;
endproperty
assert property (done_after_start);
```

## Simulation Control

```systemverilog
// Timeout
initial begin
  #10000;
  $display("TIMEOUT");
  $finish;
end

// Monitor
always @(posedge clk) begin
  $display("Time=%0t reset=%b result=%d", $time, reset, result);
end
```
