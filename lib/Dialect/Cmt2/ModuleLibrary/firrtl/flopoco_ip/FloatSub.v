// FloatSub - IEEE 754 Floating-Point Subtraction using FloPoCo IEEEFMA
// Implements: result = operand0 - operand1 = 1.0 * operand0 - operand1
// Fully pipelined design with II=1

module FloatSub #(
    parameter WIDTH = 32,
    parameter LATENCY = 4  // FloPoCo FMA supports: 1, 2, 3, 4, 6 for float32
)(
    input  wire             clock,
    input  wire             reset,
    input  wire             ce,
    input  wire [WIDTH-1:0] operand0,
    input  wire [WIDTH-1:0] operand1,
    output wire [WIDTH-1:0] result,
    output wire             valid,
    output wire             input_ready  // Always ready (fully pipelined, II=1)
);

    // IEEE 754 constant: 1.0
    localparam [WIDTH-1:0] ONE = (WIDTH == 16) ? 16'h3C00 :
                                 (WIDTH == 32) ? 32'h3F800000 :
                                 64'h3FF0000000000000;

    // FloPoCo IEEEFMA: (A * B) + C with negateC
    // Sub: result = 1.0 * operand0 - operand1
    IEEEFMA #(
        .DataWidth( WIDTH   ),
        .Latency  ( LATENCY )
    ) i_fma (
        .clk_i      ( clock    ),
        .rst_ni     ( !reset   ),
        .operand_a_i( ONE      ),      // A = 1.0
        .operand_b_i( operand0 ),      // B = operand0
        .operand_c_i( operand1 ),      // C = operand1
        .negate_a_i ( 1'b0     ),      // No negation on A*B
        .negate_c_i ( 1'b1     ),      // Negate C: result = A*B - C
        .result_o   ( result   )
    );

    // Valid signal pipeline only
    reg [LATENCY-1:0] valid_pipeline;

    always @(posedge clock) begin
        if (reset) begin
            valid_pipeline <= {LATENCY{1'b0}};
        end else begin
            valid_pipeline <= {valid_pipeline[LATENCY-2:0], ce};
        end
    end

    assign valid = valid_pipeline[LATENCY-1];
    assign input_ready = 1'b1;  // Always ready (II=1)

endmodule
