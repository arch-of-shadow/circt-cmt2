module FloatAdd #(
    parameter WIDTH = 32,
    parameter LATENCY = 1 //cannoot be 0
)(
    input  wire             clock,
    input  wire             reset,
    input  wire             ce,
    input  wire [WIDTH-1:0] operand0,
    input  wire [WIDTH-1:0] operand1,
    output wire [WIDTH-1:0] result,
    output wire             valid,
    output wire             input_ready  // Always ready (static scheduling)
);
    localparam expWidth = (WIDTH == 16) ? 5 :
                          (WIDTH == 32) ? 8 :
                          (WIDTH == 64) ? 11 : 15;
    localparam sigWidth = WIDTH - expWidth;

    wire [WIDTH:0] recA, recB, recResult;
    wire [WIDTH-1:0] result_0;
    reg  [WIDTH  -1:0] result_reg [0:LATENCY-1];
    reg  [LATENCY-1:0] valid_reg;

    fNToRecFN #(expWidth, sigWidth) cvtA (
        .in(operand0), .out(recA)
    );
    fNToRecFN #(expWidth, sigWidth) cvtB (
        .in(operand1), .out(recB)
    );


    wire [4:0] exceptionFlags;
    addRecFN #(expWidth, sigWidth) adder (
        .control(1'b0),
        .subOp(1'b0),
        .a(recA),
        .b(recB),
        .roundingMode(3'b000),  // round to nearest even
        .out(recResult),
        .exceptionFlags(exceptionFlags)
    );


    recFNToFN #(expWidth, sigWidth) cvtResult (
        .in(recResult), .out(result_0)
    );

    // assign valid = ce;
    generate 
        for (genvar i = 0; i < LATENCY; i = i + 1) begin : valid_pipeline
            if (i == 0) begin
                always @(posedge clock) begin
                    if (reset) begin
                        valid_reg[i] <= 1'b0;
                        result_reg[i] <= {WIDTH{1'b0}};
                    end else if (ce) begin
                        valid_reg[i] <= 1'b1;
                        result_reg[i] <= result_0;
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
    endgenerate
    assign valid = valid_reg[LATENCY-1];
    assign result = result_reg[LATENCY-1];
    assign input_ready = 1'b1;  // Always ready (static scheduling, no backpressure)
endmodule