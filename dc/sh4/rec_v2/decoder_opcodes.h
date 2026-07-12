#pragma once
#define sh4dec(str) void dec_##str (u32 op)

   
sh4dec(i1000_1011_iiii_iiii);
sh4dec(i1000_1111_iiii_iiii);
sh4dec(i1000_1001_iiii_iiii);
sh4dec(i1000_1101_iiii_iiii);
sh4dec(i1010_iiii_iiii_iiii);
sh4dec(i0000_nnnn_0010_0011);
sh4dec(i0100_nnnn_0010_1011);
sh4dec(i1011_iiii_iiii_iiii);
sh4dec(i0000_nnnn_0000_0011);
sh4dec(i0100_nnnn_0000_1011);
sh4dec(i0000_0000_0000_1011);
sh4dec(i0000_0000_0010_1011);   
sh4dec(i1100_0011_iiii_iiii);
sh4dec(i0000_0000_0001_1011);
//sh4dec(i0100_nnnn_0000_0111);
//sh4dec(i0100_nnnn_0000_1110);
sh4dec(i0011_nnnn_mmmm_1000);    
sh4dec(i0011_nnnn_mmmm_1100);
sh4dec(i0111_nnnn_iiii_iiii);
sh4dec(i0000_0000_0000_1001);
sh4dec(i1111_0011_1111_1101);

// Additional handlers from Flycast
sh4dec(i1111_1011_1111_1101);  // frchg
sh4dec(i0100_nnnn_0010_0100);  // rotcl <REG_N>
sh4dec(i0100_nnnn_0010_0101);  // rotcr <REG_N>
sh4dec(i0100_nnnn_0110_1010);  // ldc <REG_N>,FPSCR
sh4dec(i0100_nnnn_0110_0110);  // ldc.l @<REG_N>+,FPSCR
sh4dec(i0100_nnnn_0001_1011);  // tas.b @<REG_N>
sh4dec(i0100_nnnn_0000_0011);  // stc.l SR,@-<REG_N>
sh4dec(i0010_nnnn_mmmm_1101);  // xtrct <REG_M>,<REG_N>

void dec_illegalOp(u32 op);