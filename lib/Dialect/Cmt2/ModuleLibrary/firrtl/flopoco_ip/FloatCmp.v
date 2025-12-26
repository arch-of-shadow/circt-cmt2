// FloatCmp - IEEE 754 Floating-Point Comparison using FloPoCo IEEEComp + FIFO
// Implements: result = compare(operand0, operand1) based on predicate
// Predicate: 0=eq, 1=lt, 2=le, 3=gt, 4=ge, 5=ne, 6=ord, 7=uno
// Combinational comparison with optional pipeline + FIFO buffering

module FloatCmp #(
    parameter WIDTH = 32,
    parameter PREDICATE = 0,  // 0=eq, 1=lt, 2=le, 3=gt, 4=ge, 5=ne, 6=ord, 7=uno
    parameter LATENCY = 2     // Total latency including FIFO
)(
    input  wire             clock,
    input  wire             reset,
    input  wire             ce,
    input  wire [WIDTH-1:0] operand0,
    input  wire [WIDTH-1:0] operand1,
    input  wire             rd_en,       // FIFO read enable
    output wire             rd_data,     // FIFO read data (1-bit result)
    output wire             rd_ready,    // FIFO has data
    output wire             input_ready  // Always ready (static scheduling)
);

    localparam FIFO_DEPTH = LATENCY;
    localparam PIPE_STAGES = (LATENCY > 1) ? (LATENCY - 1) : 0;

    wire a_lt_b, a_eq_b, a_gt_b, a_le_b, a_ge_b, unordered;
    reg  cmp_result;

    // FloPoCo IEEEComp: IEEE 754 comparator
    IEEEComp #(
        .DataWidth( WIDTH )
    ) i_cmp (
        .operand_a_i( operand0   ),
        .operand_b_i( operand1   ),
        .a_lt_b_o   ( a_lt_b     ),
        .a_eq_b_o   ( a_eq_b     ),
        .a_gt_b_o   ( a_gt_b     ),
        .a_le_b_o   ( a_le_b     ),
        .a_ge_b_o   ( a_ge_b     ),
        .unordered_o( unordered  )
    );

    // Select result based on predicate
    always @(*) begin
        case (PREDICATE)
            0: cmp_result = a_eq_b;           // eq
            1: cmp_result = a_lt_b;           // lt
            2: cmp_result = a_le_b;           // le
            3: cmp_result = a_gt_b;           // gt
            4: cmp_result = a_ge_b;           // ge
            5: cmp_result = !a_eq_b;          // ne
            6: cmp_result = !unordered;       // ord (ordered)
            7: cmp_result = unordered;        // uno (unordered)
            default: cmp_result = 1'b0;
        endcase
    end

    // Pipeline and FIFO logic
    wire fifo_wr_en;
    wire fifo_wr_data;

    generate
        if (PIPE_STAGES == 0) begin : gen_no_pipeline
            // No pipeline stages, direct to FIFO
            assign fifo_wr_en = ce;
            assign fifo_wr_data = cmp_result;
        end else if (PIPE_STAGES == 1) begin : gen_one_stage
            // Single pipeline stage
            reg result_reg;
            reg valid_reg;

            always @(posedge clock) begin
                if (reset) begin
                    result_reg <= 1'b0;
                    valid_reg <= 1'b0;
                end else begin
                    result_reg <= cmp_result;
                    valid_reg <= ce;
                end
            end

            assign fifo_wr_en = valid_reg;
            assign fifo_wr_data = result_reg;
        end else begin : gen_multi_stage
            // Multiple pipeline stages
            reg [PIPE_STAGES-1:0] result_pipeline;
            reg [PIPE_STAGES-1:0] valid_pipeline;

            always @(posedge clock) begin
                if (reset) begin
                    result_pipeline <= 0;
                    valid_pipeline <= 0;
                end else begin
                    result_pipeline <= {result_pipeline[PIPE_STAGES-2:0], cmp_result};
                    valid_pipeline <= {valid_pipeline[PIPE_STAGES-2:0], ce};
                end
            end

            assign fifo_wr_en = valid_pipeline[PIPE_STAGES-1];
            assign fifo_wr_data = result_pipeline[PIPE_STAGES-1];
        end
    endgenerate

    // FIFO for result buffering
    fifo #(
        .WIDTH(1),
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
