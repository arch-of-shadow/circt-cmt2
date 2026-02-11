// Simple extern module used by PyCMT2 examples to demonstrate multicycle
// call semantics in proc.static_step.
//
// Interface is defined by ExternalModuleBuilder in:
//   examples/PyCMT2/static_step_call_timing_window.py
//
// Semantics:
// - A 1-cycle pulse on `run_enable` starts an operation.
// - After a fixed latency (LAT cycles), the counter increments once.
// - `run_out` exposes the counter value continuously, but is only "valid"
//   at the capture cycle specified by result_timing in the caller.

module EnableWindowCounter(
  input  logic        clk,
  input  logic        rst,

  input  logic        run_enable,
  output logic        run_ready,

  input  logic        dummy,
  output logic [31:0] run_out
);

  localparam int LAT = 6;

  logic [31:0] count;
  logic [LAT-1:0] pipe;

  assign run_ready = 1'b1;
  // Present the "completed" value in the completion cycle so callers can
  // capture it at the end of that cycle (posedge). This matches the CMT2
  // `result_timing` notion of a capture cycle.
  assign run_out = count + {{31{1'b0}}, pipe[LAT-1]};

  always_ff @(posedge clk) begin
    if (rst) begin
      count <= 32'd0;
      pipe <= '0;
    end else begin
      pipe <= {pipe[LAT-2:0], run_enable};
      if (pipe[LAT-1])
        count <= count + 32'd1;
    end
  end

endmodule
