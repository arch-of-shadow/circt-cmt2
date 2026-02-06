// Simple extern module used by PyCMT2 examples to demonstrate how
// cmt2.call timing (arg_timing/result_timing) affects enable gating inside
// a proc.static_step after lowering to SV.
//
// Interface is defined by ExternalModuleBuilder in:
//   examples/PyCMT2/static_step_call_timing_window.py
//
// Semantics:
// - Counter increments on each cycle `run_enable` is high.
// - `out` exposes the counter value continuously.

module EnableWindowCounter(
  input  logic        clk,
  input  logic        rst,

  input  logic        run_enable,
  output logic        run_ready,

  input  logic        dummy,
  output logic        dummy_out,

  output logic [31:0] out
);

  logic [31:0] count;

  assign run_ready = 1'b1;
  assign dummy_out = 1'b0;
  assign out = count;

  always_ff @(posedge clk) begin
    if (rst) begin
      count <= 32'd0;
    end else if (run_enable) begin
      count <= count + 32'd1;
    end
  end

endmodule

