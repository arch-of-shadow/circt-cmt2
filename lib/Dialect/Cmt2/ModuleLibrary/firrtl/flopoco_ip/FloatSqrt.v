// FloatSqrt - IEEE 754 Floating-Point Square Root using FloPoCo + FIFO
// Fully pipelined design with II=1, result buffered in FIFO

module FloatSqrt #(
    parameter WIDTH = 32,
    parameter LATENCY = 13  // Total latency including FIFO (must be >= 3)
)(
    input  wire             clock,
    input  wire             reset,
    input  wire             ce,
    input  wire [WIDTH-1:0] operand0,  // radicand
    input  wire             rd_en,       // FIFO read enable
    output wire [WIDTH-1:0] rd_data,     // FIFO read data (result)
    output wire             rd_ready,    // FIFO has data
    output wire             input_ready  // Always ready (static scheduling)
);

    localparam FLOAT_LATENCY = LATENCY - 1;
    localparam FIFO_DEPTH = LATENCY;

    wire [WIDTH-1:0] sqrt_result;

    // FloPoCo IEEESqrt instance (already pipelined internally)
    IEEESqrt #(
        .DataWidth( WIDTH         ),
        .Latency  ( FLOAT_LATENCY )
    ) i_sqrt (
        .clk_i      ( clock    ),
        .rst_ni     ( !reset   ),
        .operand_x_i( operand0 ),
        .result_o   ( sqrt_result )
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
        .wr_data(sqrt_result),
        .wr_ready(),  // Not used (static scheduling)
        .rd_en(rd_en),
        .rd_data(rd_data),
        .rd_ready(rd_ready)
    );

    assign input_ready = 1'b1;  // Always ready (static scheduling)

endmodule
