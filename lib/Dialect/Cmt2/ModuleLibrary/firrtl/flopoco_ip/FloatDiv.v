// FloatDiv - IEEE 754 Floating-Point Division using FloPoCo
// Fully pipelined design with II=1

module FloatDiv #(
    parameter WIDTH = 32,
    parameter LATENCY = 12  // FloPoCo supports: 2, 4, 5, 7, 8, 12 for float32
)(
    input  wire             clock,
    input  wire             reset,
    input  wire             ce,
    input  wire [WIDTH-1:0] operand0,  // dividend
    input  wire [WIDTH-1:0] operand1,  // divisor
    output wire [WIDTH-1:0] result,
    output wire             valid,
    output wire             input_ready  // Always ready (fully pipelined, II=1)
);

    // FloPoCo IEEEDiv instance (already pipelined internally)
    IEEEDiv #(
        .DataWidth( WIDTH   ),
        .Latency  ( LATENCY )
    ) i_div (
        .clk_i      ( clock    ),
        .rst_ni     ( !reset   ),
        .operand_a_i( operand0 ),
        .operand_b_i( operand1 ),
        .result_o   ( result   )
    );

    // Valid signal pipeline only (result already pipelined inside FloPoCo)
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
