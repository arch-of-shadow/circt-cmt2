// FloatSqrt - IEEE 754 Floating-Point Square Root using HardFloat
// Uses divSqrtRecFN_small (multi-cycle iterative)

module FloatSqrt #(
    parameter WIDTH = 32,
    parameter LATENCY = 1
)(
    input  wire             clock,
    input  wire             reset,
    input  wire             ce,
    input  wire [WIDTH-1:0] operand0,  // radicand
    output wire [WIDTH-1:0] result,
    output wire             valid,
    output wire             input_ready  // Always ready (static scheduling)
);
    localparam expWidth = (WIDTH == 16) ? 5 :
                          (WIDTH == 32) ? 8 :
                          (WIDTH == 64) ? 11 : 15;
    localparam sigWidth = WIDTH - expWidth;
    // Sqrt cycleNum from divSqrtRecFN_small:
    //   sExpA[0]=1 (odd exp):  cycleNum = sigWidth,   internal = sigWidth-1 cycles
    //   sExpA[0]=0 (even exp): cycleNum = sigWidth+1, internal = sigWidth cycles
    // Use max internal cycles (sigWidth) for pipeline calculation
    localparam INTERNAL_CYCLES = sigWidth + 1;
    localparam LATENCY_REAL = (LATENCY <= INTERNAL_CYCLES) ? 0 : LATENCY - INTERNAL_CYCLES;

    wire [WIDTH:0] recA, recResult;
    wire [WIDTH:0] recB_unused = {(WIDTH+1){1'b0}};
    wire [WIDTH-1:0] result_comb;
    reg  [WIDTH-1:0] result_reg [0:LATENCY_REAL-1];
    reg  [LATENCY_REAL-1:0] valid_reg;

    // Convert IEEE 754 to recoded format
    fNToRecFN #(expWidth, sigWidth) cvtA (
        .in(operand0), .out(recA)
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

    // Instantiate divSqrtRecFN_small for sqrt (sqrtOp = 1)
    divSqrtRecFN_small #(
        .expWidth(expWidth),
        .sigWidth(sigWidth),
        .options(0)
    ) sqrter (
        .nReset(!reset),
        .clock(clock),
        .control(1'b0),
        .inReady(inReady),
        .inValid(start_op),
        .sqrtOp(1'b1),              // square root
        .a(recA),
        .b(recB_unused),
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

    // =========================================================================
    // Odd exponent alignment logic
    // Odd exponent finishes 1 cycle earlier (sigWidth-1 vs sigWidth cycles)
    // We detect this by counting cycles and delay by 1 cycle to align
    // =========================================================================
    reg [$clog2(sigWidth+2)-1:0] cycle_count;

    always @(posedge clock) begin
        if (reset) begin
            cycle_count <= 0;
        end else if (start_op) begin
            cycle_count <= 1;
        end else if (input_pending && !outValid_internal) begin
            cycle_count <= cycle_count + 1;
        end else if (outValid_internal) begin
            cycle_count <= 0;
        end
    end

    // Odd exponent case: outValid_internal fires at cycle (sigWidth-1)
    wire is_odd_exp_cycle = (cycle_count == sigWidth - 1);

    // Delay odd exponent result by 1 cycle to align with even exponent timing
    reg outValid_odd_delayed;
    reg [WIDTH-1:0] result_odd_delayed;

    always @(posedge clock) begin
        if (reset) begin
            outValid_odd_delayed <= 1'b0;
            result_odd_delayed <= {WIDTH{1'b0}};
        end else begin
            outValid_odd_delayed <= outValid_internal && is_odd_exp_cycle;
            if (outValid_internal && is_odd_exp_cycle)
                result_odd_delayed <= result_comb;
        end
    end

    // Aligned output: immediate for even exp, delayed for odd exp
    wire outValid_aligned = (outValid_internal && !is_odd_exp_cycle) || outValid_odd_delayed;
    wire [WIDTH-1:0] result_aligned = outValid_odd_delayed ? result_odd_delayed : result_comb;

    // Output pipeline
    generate
        if(LATENCY_REAL == 0) begin : no_pipeline
            assign result = result_aligned;
            assign valid = outValid_aligned;
        end else begin : with_pipeline
            // Pipeline registers
            for (genvar i = 0; i < LATENCY_REAL; i = i + 1) begin : pipeline
                if (i == 0) begin
                    always @(posedge clock) begin
                        if (reset) begin
                            valid_reg[i] <= 1'b0;
                            result_reg[i] <= {WIDTH{1'b0}};
                        end else if (outValid_aligned) begin
                            valid_reg[i] <= 1'b1;
                            result_reg[i] <= result_aligned;
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
