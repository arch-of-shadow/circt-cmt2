// Int2Float - Signed Integer to IEEE 754 Floating-Point Conversion using FloPoCo Fix2FP + FIFO
// Implements: result = (float)operand0
// Converts signed integer to IEEE 754 floating-point format
// Fully pipelined design with II=1, result buffered in FIFO

module Int2Float #(
    parameter WIDTH = 32,     // Both input integer width and output float width
    parameter LATENCY = 2     // Total latency including FIFO
)(
    input  wire             clock,
    input  wire             reset,
    input  wire             ce,
    input  wire [WIDTH-1:0] operand0,    // Signed integer input
    input  wire             rd_en,       // FIFO read enable
    output wire [WIDTH-1:0] rd_data,     // FIFO read data (IEEE float result)
    output wire             rd_ready,    // FIFO has data
    output wire             input_ready  // Always ready (static scheduling)
);

    localparam CONV_LATENCY = (LATENCY > 1) ? (LATENCY - 1) : 0;
    localparam FIFO_DEPTH = LATENCY;
    localparam FP_WIDTH = WIDTH + 2;  // FloPoCo internal format

    wire [FP_WIDTH-1:0] fp_result;
    wire [WIDTH-1:0]    ieee_result;

    // FloPoCo INT2FP (Fix2FP): converts signed integer to FloPoCo FP
    INT2FP #(
        .DataWidth( WIDTH       ),
        .Latency  ( CONV_LATENCY )
    ) i_int2fp (
        .clk_i ( clock     ),
        .rst_ni( !reset    ),
        .int_i ( operand0  ),
        .fp_o  ( fp_result )
    );

    // Convert FloPoCo internal format to IEEE 754
    FP2IEEE #(
        .DataWidth( WIDTH )
    ) i_fp2ieee (
        .fp_i  ( fp_result   ),
        .ieee_o( ieee_result )
    );

    // Valid signal pipeline and FIFO
    wire fifo_wr_en;
    wire [WIDTH-1:0] fifo_wr_data;

    generate
        if (CONV_LATENCY == 0) begin : gen_no_pipeline
            assign fifo_wr_en = ce;
            assign fifo_wr_data = ieee_result;
        end else if (CONV_LATENCY == 1) begin : gen_one_stage
            reg valid_reg;

            always @(posedge clock) begin
                if (reset)
                    valid_reg <= 1'b0;
                else
                    valid_reg <= ce;
            end

            assign fifo_wr_en = valid_reg;
            assign fifo_wr_data = ieee_result;
        end else begin : gen_multi_stage
            reg [CONV_LATENCY-1:0] valid_pipeline;

            always @(posedge clock) begin
                if (reset)
                    valid_pipeline <= 0;
                else
                    valid_pipeline <= {valid_pipeline[CONV_LATENCY-2:0], ce};
            end

            assign fifo_wr_en = valid_pipeline[CONV_LATENCY-1];
            assign fifo_wr_data = ieee_result;
        end
    endgenerate

    fifo #(
        .WIDTH(WIDTH),
        .DEPTH(FIFO_DEPTH)
    ) result_fifo (
        .clk(clock),
        .reset(reset),
        .wr_en(fifo_wr_en),
        .wr_data(fifo_wr_data),
        .wr_ready(),
        .rd_en(rd_en),
        .rd_data(rd_data),
        .rd_ready(rd_ready)
    );

    assign input_ready = 1'b1;

endmodule
