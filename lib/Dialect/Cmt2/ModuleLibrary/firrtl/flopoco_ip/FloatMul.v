// FloatMul - IEEE 754 Floating-Point Multiplication using FloPoCo IEEEFMA
// Implements: result = operand0 * operand1 = operand0 * operand1 + 0.0
// Fully pipelined design with II=1

module FloatMul #(
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

    // IEEE 754 constant: 0.0
    localparam [WIDTH-1:0] ZERO = {WIDTH{1'b0}};

    // FloPoCo IEEEFMA: (A * B) + C
    // Mul: result = operand0 * operand1 + 0.0
    IEEEFMA #(
        .DataWidth( WIDTH   ),
        .Latency  ( LATENCY )
    ) i_fma (
        .clk_i      ( clock    ),
        .rst_ni     ( !reset   ),
        .operand_a_i( operand0 ),      // A = operand0
        .operand_b_i( operand1 ),      // B = operand1
        .operand_c_i( ZERO     ),      // C = 0.0
        .negate_a_i ( 1'b0     ),      // No negation
        .negate_c_i ( 1'b0     ),      // No negation
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
