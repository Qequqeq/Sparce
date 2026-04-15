module delay #( parameter
  LENGTH = 2,                  // delay/synchronizer chain length
  WIDTH = 1                    // signal width
)(
  input clk,
  input nrst,
  input ena,

  input [WIDTH-1:0] in,
  output [WIDTH-1:0] out
);

  reg [LENGTH:1][WIDTH-1:0] data = 0;

  always @(posedge clk) begin
    integer i;
    if( ~nrst ) begin
      data <= 0;
    end else if( ena ) begin
      for( i=LENGTH-1; i>0; i=i-1 ) begin
        data[i+1][WIDTH-1:0] <= data[i][WIDTH-1:0];
      end
      data[1][WIDTH-1:0] <= in[WIDTH-1:0];
    end
  end

  assign out[WIDTH-1:0] = data[LENGTH][WIDTH-1:0];

endmodule
