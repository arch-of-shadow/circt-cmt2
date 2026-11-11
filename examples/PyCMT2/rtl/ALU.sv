module ALU(
  input  logic [7:0] a,
  input  logic [7:0] b,
  output logic [7:0] out
);
  always_comb out = a + b;
endmodule

