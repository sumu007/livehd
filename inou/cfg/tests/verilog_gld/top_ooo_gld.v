/* Generated by Yosys 0.7+605 (git sha1 d412b172, clang 6.0.0 -fPIC -Os) */

module top_ooo(a, b, out);
  input a;
  input b;
  output [1:0] out;
  sp_add lgraph_id_0 (
    .a(a),
    .b(b),
    .s(out)
  );
endmodule
