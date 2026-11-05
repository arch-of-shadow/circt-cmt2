module Accum32(
  input  logic        clk,
  input  logic        rst,
  output logic        read_ready,
  output logic [31:0] read_data,
  input  logic        add_enable,
  output logic        add_ready,
  input  logic [31:0] add_data
);
  logic [31:0] sum;

  assign read_ready = 1'b1;
  assign add_ready  = 1'b1;
  assign read_data  = sum;

  always_ff @(posedge clk) begin
    if (rst) begin
      sum <= '0;
    end else if (add_enable) begin
      sum <= sum + add_data;
    end
  end
endmodule

