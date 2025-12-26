// FloatLog - IEEE 754 Floating-Point Natural Logarithm using FloPoCo FPLog + FIFO
// Implements: result = ln(operand0)
// Fully pipelined design with II=1, result buffered in FIFO
// Note: FPLog uses FloPoCo internal format (WIDTH+2 bits), so IEEE2FP/FP2IEEE conversion needed

module FloatLog #(
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
    localparam FP_WIDTH = WIDTH + 2;  // FloPoCo internal format

    wire [FP_WIDTH-1:0] fp_operand;
    wire [FP_WIDTH-1:0] fp_result;
    wire [WIDTH-1:0]    ieee_result;

    // Convert IEEE 754 to FloPoCo internal format
    IEEE2FP #(
        .DataWidth( WIDTH )
    ) i_ieee2fp (
        .ieee_i( operand0   ),
        .fp_o  ( fp_operand )
    );

    // FloPoCo FPLog: computes ln(x)
    FPLog #(
        .DataWidth( FP_WIDTH      ),
        .Latency  ( FLOAT_LATENCY )
    ) i_log (
        .clk_i      ( clock       ),
        .rst_ni     ( !reset      ),
        .operand_x_i( fp_operand  ),
        .result_o   ( fp_result   )
    );

    // Convert FloPoCo internal format back to IEEE 754
    FP2IEEE #(
        .DataWidth( WIDTH )
    ) i_fp2ieee (
        .fp_i  ( fp_result   ),
        .ieee_o( ieee_result )
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
        .wr_data(ieee_result),
        .wr_ready(),  // Not used (static scheduling)
        .rd_en(rd_en),
        .rd_data(rd_data),
        .rd_ready(rd_ready)
    );

    assign input_ready = 1'b1;  // Always ready (static scheduling)

endmodule
