// FloatAdd - IEEE 754 Floating-Point Addition using FloPoCo IEEEFMA + FIFO
// Implements: result = operand0 + operand1 = 1.0 * operand0 + operand1
// Fully pipelined design with II=1, result buffered in FIFO

module FloatAdd #(
    parameter WIDTH = 32,
    parameter LATENCY = 5  // Total latency including FIFO (must be >= 2)
)(
    input  wire             clock,
    input  wire             reset,
    input  wire             ce,
    input  wire [WIDTH-1:0] operand0,
    input  wire [WIDTH-1:0] operand1,
    input  wire             rd_en,       // FIFO read enable
    output wire [WIDTH-1:0] rd_data,     // FIFO read data (result)
    output wire             rd_ready,    // FIFO has data
    output wire             input_ready  // Always ready (static scheduling)
);

    localparam FLOAT_LATENCY = LATENCY - 1;
    localparam FIFO_DEPTH = LATENCY;

    // IEEE 754 constant: 1.0
    localparam [WIDTH-1:0] ONE = (WIDTH == 16) ? 16'h3C00 :   // fp16: 1.0
                                 (WIDTH == 32) ? 32'h3F800000 : // fp32: 1.0
                                 64'h3FF0000000000000;          // fp64: 1.0

    wire [WIDTH-1:0] fma_result;

    // FloPoCo IEEEFMA: (A * B) + C
    // Add: result = 1.0 * operand0 + operand1
    IEEEFMA #(
        .DataWidth( WIDTH         ),
        .Latency  ( FLOAT_LATENCY )
    ) i_fma (
        .clk_i      ( clock    ),
        .rst_ni     ( !reset   ),
        .operand_a_i( ONE      ),      // A = 1.0
        .operand_b_i( operand0 ),      // B = operand0
        .operand_c_i( operand1 ),      // C = operand1
        .negate_a_i ( 1'b0     ),      // No negation
        .negate_c_i ( 1'b0     ),      // No negation
        .result_o   ( fma_result )
    );

    // Valid signal pipeline
    reg [FLOAT_LATENCY-1:0] valid_pipeline;

    always @(posedge clock) begin
        if (reset) begin
            valid_pipeline <= {FLOAT_LATENCY{1'b0}};
        end else begin
            valid_pipeline <= {valid_pipeline[FLOAT_LATENCY-2:0], ce};
        end
    end

    wire float_valid = valid_pipeline[FLOAT_LATENCY-1];

    // Instantiate FIFO for result buffering
    fifo #(
        .WIDTH(WIDTH),
        .DEPTH(FIFO_DEPTH)
    ) result_fifo (
        .clk(clock),
        .reset(reset),
        .wr_en(float_valid),
        .wr_data(fma_result),
        .wr_ready(),  // Not used (static scheduling)
        .rd_en(rd_en),
        .rd_data(rd_data),
        .rd_ready(rd_ready)
    );

    assign input_ready = 1'b1;  // Always ready (static scheduling)

endmodule
