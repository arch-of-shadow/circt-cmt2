// FloatDiv - IEEE 754 Floating-Point Division using HardFloat
// Uses divSqrtRecFN_small (multi-cycle iterative divider)

module FloatDiv #(
    parameter WIDTH = 32,
    parameter LATENCY = 1
)(
    input  wire             clock,
    input  wire             reset,
    input  wire             ce,
    input  wire [WIDTH-1:0] operand0,  // dividend
    input  wire [WIDTH-1:0] operand1,  // divisor
    output wire [WIDTH-1:0] result,
    output wire             valid,
    output wire             input_ready  // Always ready (static scheduling)
);
    localparam expWidth = (WIDTH == 16) ? 5 :
                          (WIDTH == 32) ? 8 :
                          (WIDTH == 64) ? 11 : 15;
    localparam sigWidth = WIDTH - expWidth;
    // Division: cycleNum = sigWidth + 2, internal cycles = sigWidth + 1
    // (counts from sigWidth+2 down to 1, so sigWidth+2-1 = sigWidth+1 cycles)
    localparam INTERNAL_CYCLES = sigWidth + 1;
    localparam LATENCY_REAL = (LATENCY <= INTERNAL_CYCLES) ? 0 : LATENCY - INTERNAL_CYCLES;

    wire [WIDTH:0] recA, recB, recResult;
    wire [WIDTH-1:0] result_comb;
    reg  [WIDTH-1:0] result_reg [0:LATENCY_REAL-1];
    reg  [LATENCY_REAL-1:0] valid_reg;

    // Convert IEEE 754 to recoded format
    fNToRecFN #(expWidth, sigWidth) cvtA (
        .in(operand0), .out(recA)
    );
    fNToRecFN #(expWidth, sigWidth) cvtB (
        .in(operand1), .out(recB)
    );

    // Control signals
    wire inReady;
    wire outValid_internal;
    wire sqrtOpOut;
    wire [4:0] exceptionFlags;

    // Start operation when ce is high and unit is ready
    reg input_pending;
    wire start_op = ce && inReady && !input_pending;

    always @(posedge clock) begin
        if (reset) begin
            input_pending <= 1'b0;
        end else if (start_op) begin
            input_pending <= 1'b1;
        end else if (outValid_internal) begin
            input_pending <= 1'b0;
        end
    end

    // Instantiate divSqrtRecFN_small for division (sqrtOp = 0)
    divSqrtRecFN_small #(
        .expWidth(expWidth),
        .sigWidth(sigWidth),
        .options(0)
    ) divider (
        .nReset(!reset),
        .clock(clock),
        .control(1'b0),
        .inReady(inReady),
        .inValid(start_op),
        .sqrtOp(1'b0),              // division
        .a(recA),
        .b(recB),
        .roundingMode(3'b000),
        .outValid(outValid_internal),
        .sqrtOpOut(sqrtOpOut),
        .out(recResult),
        .exceptionFlags(exceptionFlags)
    );

    // Convert back to IEEE 754 format
    recFNToFN #(expWidth, sigWidth) cvtResult (
        .in(recResult), .out(result_comb)
    );

    // Output pipeline
    generate
        if(LATENCY_REAL == 0) begin : no_pipeline
            assign result = result_comb;
            assign valid = outValid_internal;
        end else begin : with_pipeline
            // Pipeline registers
            for (genvar i = 0; i < LATENCY_REAL; i = i + 1) begin : pipeline
                if (i == 0) begin
                    always @(posedge clock) begin
                        if (reset) begin
                            valid_reg[i] <= 1'b0;
                            result_reg[i] <= {WIDTH{1'b0}};
                        end else if (outValid_internal) begin
                            valid_reg[i] <= 1'b1;
                            result_reg[i] <= result_comb;
                        end else begin
                            valid_reg[i] <= 1'b0;
                            result_reg[i] <= result_reg[i];
                        end
                    end
                end else begin
                    always @(posedge clock) begin
                        if (reset) begin
                            valid_reg[i] <= 1'b0;
                            result_reg[i] <= {WIDTH{1'b0}};
                        end else begin
                            valid_reg[i] <= valid_reg[i-1];
                            result_reg[i] <= result_reg[i-1];
                        end
                    end
                end
            end
            assign result = result_reg[LATENCY_REAL-1];
            assign valid = valid_reg[LATENCY_REAL-1];
        end
    endgenerate

    assign input_ready = 1'b1;  // Always ready (static scheduling, no backpressure)

endmodule
