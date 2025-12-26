// Float2Int - IEEE 754 Floating-Point to Signed Integer Conversion using FloPoCo FP2Fix + FIFO
// Implements: result = (int)operand0
// Converts IEEE 754 floating-point to signed integer format
// Fully pipelined design with II=1, result buffered in FIFO

module Float2Int #(
    parameter WIDTH = 32,     // Both input float width and output integer width
    parameter LATENCY = 2     // Total latency including FIFO
)(
    input  wire             clock,
    input  wire             reset,
    input  wire             ce,
    input  wire [WIDTH-1:0] operand0,    // IEEE float input
    input  wire             rd_en,       // FIFO read enable
    output wire [WIDTH-1:0] rd_data,     // FIFO read data (signed integer result)
    output wire             rd_ready,    // FIFO has data
    output wire             input_ready  // Always ready (static scheduling)
);

    localparam CONV_LATENCY = (LATENCY > 1) ? (LATENCY - 1) : 0;
    localparam FIFO_DEPTH = LATENCY;
    localparam FP_WIDTH = WIDTH + 2;  // FloPoCo internal format

    wire [FP_WIDTH-1:0] fp_operand;
    wire [WIDTH-1:0]    int_result;
    wire                overflow;  // Not exposed in current interface

    // Convert IEEE 754 to FloPoCo internal format
    IEEE2FP #(
        .DataWidth( WIDTH )
    ) i_ieee2fp (
        .ieee_i( operand0   ),
        .fp_o  ( fp_operand )
    );

    // FloPoCo FP2INT (FP2Fix): converts FloPoCo FP to signed integer
    FP2INT #(
        .DataWidth( WIDTH        ),
        .Latency  ( CONV_LATENCY )
    ) i_fp2int (
        .clk_i     ( clock      ),
        .rst_ni    ( !reset     ),
        .fp_i      ( fp_operand ),
        .int_o     ( int_result ),
        .overflow_o( overflow   )
    );

    // Valid signal pipeline and FIFO
    wire fifo_wr_en;
    wire [WIDTH-1:0] fifo_wr_data;

    generate
        if (CONV_LATENCY == 0) begin : gen_no_pipeline
            assign fifo_wr_en = ce;
            assign fifo_wr_data = int_result;
        end else if (CONV_LATENCY == 1) begin : gen_one_stage
            reg valid_reg;

            always @(posedge clock) begin
                if (reset)
                    valid_reg <= 1'b0;
                else
                    valid_reg <= ce;
            end

            assign fifo_wr_en = valid_reg;
            assign fifo_wr_data = int_result;
        end else begin : gen_multi_stage
            reg [CONV_LATENCY-1:0] valid_pipeline;

            always @(posedge clock) begin
                if (reset)
                    valid_pipeline <= 0;
                else
                    valid_pipeline <= {valid_pipeline[CONV_LATENCY-2:0], ce};
            end

            assign fifo_wr_en = valid_pipeline[CONV_LATENCY-1];
            assign fifo_wr_data = int_result;
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
