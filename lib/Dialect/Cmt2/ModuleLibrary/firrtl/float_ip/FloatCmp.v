// FloatCmp - IEEE 754 Floating-Point Comparison using HardFloat
// Predicate: 0=eq, 1=lt, 2=le, 3=gt, 4=ge, 5=ne, 6=ord, 7=uno

module FloatCmp #(
    parameter WIDTH = 32,
    parameter PREDICATE = 0,
    parameter LATENCY = 1
)(
    input  wire             clock,
    input  wire             reset,
    input  wire             ce,
    input  wire [WIDTH-1:0] operand0,
    input  wire [WIDTH-1:0] operand1,
    output wire             result,
    output wire             valid,
    output wire             input_ready  // Always ready (static scheduling)
);
    localparam expWidth = (WIDTH == 16) ? 5 :
                          (WIDTH == 32) ? 8 :
                          (WIDTH == 64) ? 11 : 15;
    localparam sigWidth = WIDTH - expWidth;

    wire [WIDTH:0] recA, recB;
    reg  result_reg [0:LATENCY-1];
    reg  [LATENCY-1:0] valid_reg;

    fNToRecFN #(expWidth, sigWidth) cvtA (
        .in(operand0), .out(recA)
    );
    fNToRecFN #(expWidth, sigWidth) cvtB (
        .in(operand1), .out(recB)
    );

    wire lt, eq, gt, unordered;
    wire [4:0] exceptionFlags;
    wire signaling = (PREDICATE <= 5) ? 1'b1 : 1'b0;

    compareRecFN #(expWidth, sigWidth) comparator (
        .a(recA),
        .b(recB),
        .signaling(signaling),
        .lt(lt),
        .eq(eq),
        .gt(gt),
        .unordered(unordered),
        .exceptionFlags(exceptionFlags)
    );

    reg cmp_result;
    always @(*) begin
        case (PREDICATE)
            0: cmp_result = eq;
            1: cmp_result = lt;
            2: cmp_result = lt | eq;
            3: cmp_result = gt;
            4: cmp_result = gt | eq;
            5: cmp_result = !eq & !unordered;
            6: cmp_result = !unordered;
            7: cmp_result = unordered;
            default: cmp_result = 1'b0;
        endcase
    end

    generate
        for (genvar i = 0; i < LATENCY; i = i + 1) begin : pipeline
            if (i == 0) begin
                always @(posedge clock) begin
                    if (reset) begin
                        valid_reg[i] <= 1'b0;
                        result_reg[i] <= 1'b0;
                    end else if (ce) begin
                        valid_reg[i] <= 1'b1;
                        result_reg[i] <= cmp_result;
                    end else begin
                        valid_reg[i] <= 1'b0;
                        result_reg[i] <= result_reg[i];
                    end
                end
            end else begin
                always @(posedge clock) begin
                    if (reset) begin
                        valid_reg[i] <= 1'b0;
                        result_reg[i] <= 1'b0;
                    end else begin
                        valid_reg[i] <= valid_reg[i-1];
                        result_reg[i] <= result_reg[i-1];
                    end
                end
            end
        end
    endgenerate

    assign result = result_reg[LATENCY-1];
    assign valid = valid_reg[LATENCY-1];
    assign input_ready = 1'b1;  // Always ready (static scheduling, no backpressure)

endmodule
