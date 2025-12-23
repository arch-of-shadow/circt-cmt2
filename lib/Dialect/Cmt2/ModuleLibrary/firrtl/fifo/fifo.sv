module fifo #(
    parameter int WIDTH = 8,
    parameter int DEPTH = 10
) (
    input  logic             clk,
    input  logic             reset,

    // Write interface
    input  logic             wr_en,
    input  logic [WIDTH-1:0] wr_data,
    output logic             wr_ready,

    // Read interface
    input  logic             rd_en,
    output logic [WIDTH-1:0] rd_data,
    output logic             rd_ready
);

    localparam int ADDR_WIDTH = $clog2(DEPTH);
    localparam int CNT_WIDTH  = $clog2(DEPTH + 1);

    // Memory array
    logic [WIDTH-1:0] mem [DEPTH];

    // Pointers
    logic [ADDR_WIDTH-1:0] wr_ptr;
    logic [ADDR_WIDTH-1:0] rd_ptr;

    // Count
    logic [CNT_WIDTH-1:0] count;

    // Status signals
    assign full  = (count == DEPTH);
    assign empty = (count == 0);
    assign wr_ready = !full;
    assign rd_ready = !empty;

    // Write pointer
    always_ff @(posedge clk) begin
        if (reset) begin
            wr_ptr <= '0;
        end else if (wr_en && !full) begin
            mem[wr_ptr] <= wr_data;
            if (wr_ptr == DEPTH - 1)
                wr_ptr <= '0;
            else
                wr_ptr <= wr_ptr + 1'b1;
        end
    end

    // Read pointer
    always_ff @(posedge clk) begin
        if (reset) begin
            rd_ptr <= '0;
        end else if (rd_en && !empty) begin
            if (rd_ptr == DEPTH - 1)
                rd_ptr <= '0;
            else
                rd_ptr <= rd_ptr + 1'b1;
        end
    end

    // Count logic
    always_ff @(posedge clk) begin
        if (reset) begin
            count <= '0;
        end else begin
            case ({wr_en && !full, rd_en && !empty})
                2'b10:   count <= count + 1'b1;
                2'b01:   count <= count - 1'b1;
                default: count <= count;
            endcase
        end
    end

    // Read data output
    assign rd_data = mem[rd_ptr];

endmodule
