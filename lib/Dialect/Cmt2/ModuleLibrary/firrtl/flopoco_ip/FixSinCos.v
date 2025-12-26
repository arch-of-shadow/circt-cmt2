// FixSinCos - Fixed-Point Sine/Cosine using FloPoCo FixSinCos + FIFO
// Computes: sin_out = sin(pi * x), cos_out = cos(pi * x) where x in [-1, 1)
// Input/Output are Q1.PRECISION fixed-point format
// Fully pipelined design with II=1, results buffered in FIFO

module FixSinCos #(
    parameter PRECISION = 24,     // Fractional bits (24 for float32, 11 for float16)
    parameter LATENCY = 3         // Total latency including FIFO
)(
    input  wire                    clock,
    input  wire                    reset,
    input  wire                    ce,
    input  wire [PRECISION:0]      operand0,    // Q1.PRECISION fixed-point input
    input  wire                    rd_en,       // FIFO read enable
    output wire [PRECISION:0]      sin_data,    // sin(pi * operand0) output
    output wire [PRECISION:0]      cos_data,    // cos(pi * operand0) output
    output wire                    rd_ready,    // FIFO has data
    output wire                    input_ready  // Always ready (static scheduling)
);

    localparam WIDTH = PRECISION + 1;
    localparam CORE_LATENCY = LATENCY - 1;
    localparam FIFO_DEPTH = LATENCY;

    wire [WIDTH-1:0] sin_result;
    wire [WIDTH-1:0] cos_result;

    // FloPoCo FixSinCos core
    FixSinCos #(
        .Precision( PRECISION    ),
        .Latency  ( CORE_LATENCY )
    ) i_sincos (
        .clk_i ( clock      ),
        .rst_ni( !reset     ),
        .x_i   ( operand0   ),
        .sin_o ( sin_result ),
        .cos_o ( cos_result )
    );

    // Valid signal pipeline
    wire fifo_wr_en;

    generate
        if (CORE_LATENCY == 0) begin : gen_no_pipeline
            assign fifo_wr_en = ce;
        end else if (CORE_LATENCY == 1) begin : gen_one_stage
            reg valid_reg;
            always @(posedge clock) begin
                if (reset)
                    valid_reg <= 1'b0;
                else
                    valid_reg <= ce;
            end
            assign fifo_wr_en = valid_reg;
        end else begin : gen_multi_stage
            reg [CORE_LATENCY-1:0] valid_pipeline;
            always @(posedge clock) begin
                if (reset)
                    valid_pipeline <= 0;
                else
                    valid_pipeline <= {valid_pipeline[CORE_LATENCY-2:0], ce};
            end
            assign fifo_wr_en = valid_pipeline[CORE_LATENCY-1];
        end
    endgenerate

    // Concatenate sin and cos for FIFO storage
    wire [2*WIDTH-1:0] sincos_concat = {sin_result, cos_result};
    wire [2*WIDTH-1:0] sincos_out;

    // Single FIFO for both outputs
    fifo #(
        .WIDTH(2*WIDTH),
        .DEPTH(FIFO_DEPTH)
    ) result_fifo (
        .clk(clock),
        .reset(reset),
        .wr_en(fifo_wr_en),
        .wr_data(sincos_concat),
        .wr_ready(),
        .rd_en(rd_en),
        .rd_data(sincos_out),
        .rd_ready(rd_ready)
    );

    assign sin_data = sincos_out[2*WIDTH-1:WIDTH];
    assign cos_data = sincos_out[WIDTH-1:0];
    assign input_ready = 1'b1;

endmodule
