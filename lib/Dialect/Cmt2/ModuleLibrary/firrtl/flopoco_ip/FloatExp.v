// FloatExp - IEEE 754 Floating-Point Exponential (e^x) using FloPoCo IEEEExp + FIFO
// Implements: result = exp(operand0)
// Fully pipelined design with II=1, result buffered in FIFO

module FloatExp #(
    parameter WIDTH = 32,
    parameter LATENCY = 5  // Total latency including FIFO (must be >= 2)
)(
    input  wire             clock,
    input  wire             reset,
    input  wire             ce,
    input  wire [WIDTH-1:0] operand0,
    input  wire             rd_en,       // FIFO read enable
    output wire [WIDTH-1:0] rd_data,     // FIFO read data (result)
    output wire             rd_ready,    // FIFO has data
    output wire             input_ready  // Always ready (static scheduling)
);

    localparam FLOAT_LATENCY = LATENCY - 1;
    localparam FIFO_DEPTH = LATENCY;

    wire [WIDTH-1:0] exp_result;

    // FloPoCo IEEEExp: computes exp(A)
    IEEEExp #(
        .DataWidth( WIDTH         ),
        .Latency  ( FLOAT_LATENCY )
    ) i_exp (
        .clk_i      ( clock       ),
        .rst_ni     ( !reset      ),
        .operand_a_i( operand0    ),
        .result_o   ( exp_result  )
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
        .wr_data(exp_result),
        .wr_ready(),  // Not used (static scheduling)
        .rd_en(rd_en),
        .rd_data(rd_data),
        .rd_ready(rd_ready)
    );

    assign input_ready = 1'b1;  // Always ready (static scheduling)

endmodule
