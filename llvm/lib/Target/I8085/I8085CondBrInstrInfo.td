
let Uses=[SREG] in {
def SET_NE_8 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR8: $lhs,GR8: $rhs),
                        "SET_NE_8\t$dst, $lhs, $rhs", []>;

def SET_EQ_8 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR8: $lhs,GR8: $rhs),
                        "SET_EQ_8\t$dst, $lhs, $rhs", []>;

def SET_GT_8 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR8: $lhs,GR8: $rhs),
                        "SET_SGT_8\t$dst, $lhs, $rhs", []>;

def SET_GE_8 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR8: $lhs,GR8: $rhs),
                        "SET_SGE_8\t$dst, $lhs, $rhs", []>;

def SET_LT_8 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR8: $lhs,GR8: $rhs),
                        "SET_SLT_8\t$dst, $lhs, $rhs", []>;

def SET_LE_8 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR8: $lhs,GR8: $rhs),
                        "SET_SLE_8\t$dst, $lhs, $rhs", []>;   

}

let Uses=[SREG] in {

def SET_UGT_8 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR8: $lhs,GR8: $rhs),
                        "SET_UGT_8\t$dst, $lhs, $rhs", []>;

def SET_UGE_8 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR8: $lhs,GR8: $rhs),
                        "SET_UGE_8\t$dst, $lhs, $rhs", []>;

def SET_ULT_8 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR8: $lhs,GR8: $rhs),
                        "SET_ULT_8\t$dst, $lhs, $rhs", []>;

def SET_ULE_8 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR8: $lhs,GR8: $rhs),
                        "SET_ULE_8\t$dst, $lhs, $rhs", []>;   

}

let Constraints = "$lhs = $dst" , Uses=[SREG] in {

def SET_DIFF_SIGN_GT_8 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR8: $lhs,GR8: $rhs),
                        "SET_DIFF_SIGN_GT_8\t$dst, $lhs, $rhs", []>;

def SET_DIFF_SIGN_GE_8 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR8: $lhs,GR8: $rhs),
                        "SET_DIFF_SIGN_GE_8\t$dst, $lhs, $rhs", []>;

def SET_DIFF_SIGN_LT_8 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR8: $lhs,GR8: $rhs),
                        "SET_DIFF_SIGN_LT_8\t$dst, $lhs, $rhs", []>;

def SET_DIFF_SIGN_LE_8 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR8: $lhs,GR8: $rhs),
                        "SET_DIFF_SIGN_LE_8\t$dst, $lhs, $rhs", []>;                         

}



let Constraints = "$lhs = $dst" , Uses=[SREG] in {

def SET_UGT_16 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR16: $lhs,GR16: $rhs),
                        "SET_UGT_16\t$dst, $lhs, $rhs", []>;

def SET_UGE_16 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR16: $lhs,GR16: $rhs),
                        "SET_UGE_16\t$dst, $lhs, $rhs", []>;

def SET_ULT_16 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR16: $lhs,GR16: $rhs),
                        "SET_ULT_16\t$dst, $lhs, $rhs", []>;

def SET_ULE_16 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR16: $lhs,GR16: $rhs),
                        "SET_ULE_16\t$dst, $lhs, $rhs", []>;   

}

let Constraints = "$lhs = $dst" , Uses=[SREG] in {

def SET_DIFF_SIGN_GT_16 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR16: $lhs,GR16: $rhs),
                        "SET_DIFF_SIGN_GT_16\t$dst, $lhs, $rhs", []>;

def SET_DIFF_SIGN_LT_16 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR16: $lhs,GR16: $rhs),
                        "SET_DIFF_SIGN_LT_16\t$dst, $lhs, $rhs", []>;                         

}

let Constraints = "$lhs = $dst" , Uses=[SREG] in {
def SET_NE_16 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR16: $lhs,GR16: $rhs),
                        "SET_NE_16\t$dst, $lhs, $rhs", []>;

def SET_EQ_16 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR16: $lhs,GR16: $rhs),
                        "SET_EQ_16\t$dst, $lhs, $rhs", []>;

def SET_GT_16 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR16: $lhs,GR16: $rhs),
                        "SET_SGT_16\t$dst, $lhs, $rhs", []>;

def SET_GE_16 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR16: $lhs,GR16: $rhs),
                        "SET_SGE_16\t$dst, $lhs, $rhs", []>;

def SET_LT_16 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR16: $lhs,GR16: $rhs),
                        "SET_SLT_16\t$dst, $lhs, $rhs", []>;

def SET_LE_16 : I8085InserterPseudo<(outs GR8: $dst),
                        (ins GR16: $lhs,GR16: $rhs),
                        "SET_SLE_16\t$dst, $lhs, $rhs", []>;   
}

let isBranch = 1, isTerminator = 1, Uses=[SREG] in {

   def JZ :  F6<0b11001010,(outs),(ins addressTarget: $k),"JZ $k", []>;

   def JNZ :  F6<0b11000010,(outs),(ins addressTarget: $k),"JNZ $k", []>;

   def JNC :  F6<0b11010010,(outs),(ins addressTarget: $k),"JNC $k", []>;

   def JC :  F6<0b11011010,(outs),(ins addressTarget: $k),"JC $k", []>;

   def JP :  F6<0b11110010,(outs),(ins addressTarget: $k),"JP $k", []>;

   def JM :  F6<0b11111010,(outs),(ins addressTarget: $k),"JM $k", []>;

   def JPO : F6<0b11100010,(outs),(ins addressTarget: $k),"JPO $k", []>;

   def JPE : F6<0b11101010,(outs),(ins addressTarget: $k),"JPE $k", []>;
}

let isBarrier = 1, isBranch = 1, isTerminator = 1 in {
   def JMP : F6<0b11000011,(outs),(ins addressTarget:$k),
                                           "JMP $k", [(br bb: $k)]>; 
}

// Conditional returns test a status flag (Uses SREG) and, when the condition
// holds, pop the return address off the stack exactly like RET (Uses/Defs SP).
// Unlike the unconditional RET they are NOT barriers: when the condition is
// false, control falls through to the next instruction. They alter no registers
// and no flags of their own.
let isTerminator = 1, isReturn = 1, Uses = [SREG, SP], Defs = [SP] in {
   def RNZ : F1<0b11000000,(outs),(ins),"RNZ", []>;
   def RZ  : F1<0b11001000,(outs),(ins),"RZ", []>;
   def RNC : F1<0b11010000,(outs),(ins),"RNC", []>;
   def RC  : F1<0b11011000,(outs),(ins),"RC", []>;
   def RPO : F1<0b11100000,(outs),(ins),"RPO", []>;
   def RPE : F1<0b11101000,(outs),(ins),"RPE", []>;
   def RP  : F1<0b11110000,(outs),(ins),"RP", []>;
   def RM  : F1<0b11111000,(outs),(ins),"RM", []>;
}

let isCall = 1 in {
   def CNZ : F6<0b11000100,(outs),(ins imm16Addr:$k),"CNZ $k", []>;
   def CZ  : F6<0b11001100,(outs),(ins imm16Addr:$k),"CZ $k", []>;
   def CNC : F6<0b11010100,(outs),(ins imm16Addr:$k),"CNC $k", []>;
   def CC  : F6<0b11011100,(outs),(ins imm16Addr:$k),"CC $k", []>;
   def CPO : F6<0b11100100,(outs),(ins imm16Addr:$k),"CPO $k", []>;
   def CPE : F6<0b11101100,(outs),(ins imm16Addr:$k),"CPE $k", []>;
   def CP  : F6<0b11110100,(outs),(ins imm16Addr:$k),"CP $k", []>;
   def CM  : F6<0b11111100,(outs),(ins imm16Addr:$k),"CM $k", []>;
}


// Fused compare-immediate-and-branch pseudos for i8.
// These bypass the SET_*_8 diamond + JMP_8_IF pattern entirely.
let isBranch = 1, isTerminator = 1, Defs=[SREG, A] in {
   def BR_CC_EQ_8_IMM : I8085Pseudo<(outs),
       (ins GR8:$lhs, I8085Imm8:$rhs, addressTarget:$k),
       "BR_CC_EQ_8_IMM $lhs, $rhs, $k", []>;
   def BR_CC_NE_8_IMM : I8085Pseudo<(outs),
       (ins GR8:$lhs, I8085Imm8:$rhs, addressTarget:$k),
       "BR_CC_NE_8_IMM $lhs, $rhs, $k", []>;
   def BR_CC_ULT_8_IMM : I8085Pseudo<(outs),
       (ins GR8:$lhs, I8085Imm8:$rhs, addressTarget:$k),
       "BR_CC_ULT_8_IMM $lhs, $rhs, $k", []>;
   def BR_CC_UGE_8_IMM : I8085Pseudo<(outs),
       (ins GR8:$lhs, I8085Imm8:$rhs, addressTarget:$k),
       "BR_CC_UGE_8_IMM $lhs, $rhs, $k", []>;
}

// Fused compare-register-and-branch pseudos for i8 (register RHS).
// Same idea as the *_IMM forms but the RHS is a register, so they bypass the
// SET_*_8 boolean diamond + JMP_8_IF re-test for `if (a <cc> b)` with two
// variables. Expanded to MOV A,lhs ; SUB rhs ; Jcc target.
// $rhs is constrained to GR8NoAHL ({B,C,D,E}) so the register allocator cannot
// place it in A: the expansion does `MOV A,lhs` first, which would otherwise
// clobber an RHS that shared the accumulator (`SUB A` == 0).
let isBranch = 1, isTerminator = 1, Defs=[SREG, A] in {
   def BR_CC_EQ_8 : I8085Pseudo<(outs),
       (ins GR8:$lhs, GR8NoAHL:$rhs, addressTarget:$k),
       "BR_CC_EQ_8 $lhs, $rhs, $k", []>;
   def BR_CC_NE_8 : I8085Pseudo<(outs),
       (ins GR8:$lhs, GR8NoAHL:$rhs, addressTarget:$k),
       "BR_CC_NE_8 $lhs, $rhs, $k", []>;
   def BR_CC_ULT_8 : I8085Pseudo<(outs),
       (ins GR8:$lhs, GR8NoAHL:$rhs, addressTarget:$k),
       "BR_CC_ULT_8 $lhs, $rhs, $k", []>;
   def BR_CC_UGE_8 : I8085Pseudo<(outs),
       (ins GR8:$lhs, GR8NoAHL:$rhs, addressTarget:$k),
       "BR_CC_UGE_8 $lhs, $rhs, $k", []>;
}

// Fused signed compare-register-and-branch for i8. Uses the sign-bias identity
// signed(a) <cc> signed(b)  ==  unsigned(a^0x80) <cc> unsigned(b^0x80), so the
// custom inserter biases both operands by XRI 0x80 and then does an unsigned
// SUB + carry branch. Needs a temp register for the biased RHS, hence a
// custom-inserter pseudo (runs pre-RA where virtual registers are available).
let isBranch = 1, isTerminator = 1, Defs=[SREG, A] in {
   def BR_CC_SLT_8 : I8085InserterPseudo<(outs),
       (ins GR8:$lhs, GR8:$rhs, addressTarget:$k),
       "BR_CC_SLT_8 $lhs, $rhs, $k", []>;
   def BR_CC_SGE_8 : I8085InserterPseudo<(outs),
       (ins GR8:$lhs, GR8:$rhs, addressTarget:$k),
       "BR_CC_SGE_8 $lhs, $rhs, $k", []>;
}

// Fused compare-register-and-branch for i16, all condition codes. The custom
// inserter extracts the two bytes of each operand and emits a linear sequence:
//   EQ/NE       : XOR the low bytes and the high bytes, OR them (Z iff equal)
//   ULT/UGE     : SUB low, SBB high (CY = 16-bit borrow), branch on carry
//   SLT/SGE     : bias both high bytes by XRI 0x80, then the unsigned sequence
// bypassing the SET_*_16 boolean diamond entirely.
let isBranch = 1, isTerminator = 1, Defs=[SREG, A] in {
   def BR_CC_EQ_16  : I8085InserterPseudo<(outs),
       (ins GR16Pair:$lhs, GR16Pair:$rhs, addressTarget:$k), "BR_CC_EQ_16 $lhs, $rhs, $k", []>;
   def BR_CC_NE_16  : I8085InserterPseudo<(outs),
       (ins GR16Pair:$lhs, GR16Pair:$rhs, addressTarget:$k), "BR_CC_NE_16 $lhs, $rhs, $k", []>;
   def BR_CC_ULT_16 : I8085InserterPseudo<(outs),
       (ins GR16Pair:$lhs, GR16Pair:$rhs, addressTarget:$k), "BR_CC_ULT_16 $lhs, $rhs, $k", []>;
   def BR_CC_UGE_16 : I8085InserterPseudo<(outs),
       (ins GR16Pair:$lhs, GR16Pair:$rhs, addressTarget:$k), "BR_CC_UGE_16 $lhs, $rhs, $k", []>;
   def BR_CC_SLT_16 : I8085InserterPseudo<(outs),
       (ins GR16Pair:$lhs, GR16Pair:$rhs, addressTarget:$k), "BR_CC_SLT_16 $lhs, $rhs, $k", []>;
   def BR_CC_SGE_16 : I8085InserterPseudo<(outs),
       (ins GR16Pair:$lhs, GR16Pair:$rhs, addressTarget:$k), "BR_CC_SGE_16 $lhs, $rhs, $k", []>;
}

let isBranch = 1, isTerminator = 1, Defs=[SREG, A] in {
   def JMP_8_IF : I8085Pseudo<(outs),(ins GR8NoHL: $reg, addressTarget:$k),
                                           "JMP_8_IF $reg, $k", []>;

   def JMP_16_IF : I8085Pseudo<(outs),(ins GR16: $reg, addressTarget:$k),
                                           "JMP_16_IF $reg, $k", []>; 

   def JMP_16_IF_NOT_EQUAL : I8085Pseudo<(outs),(ins GR16: $lhs, GR16: $rhs, addressTarget:$k),
                                           "JMP_16_IF_NOT_EQUAL $lhs, $rhs, $k", []>; 

   def JMP_16_IF_ZERO : I8085Pseudo<(outs),(ins GR16: $reg, addressTarget:$k),
                                           "JMP_16_IF_ZERO $reg, $k", []>;

   def JMP_16_IF_NOT_ZERO : I8085Pseudo<(outs),(ins GR16: $reg, addressTarget:$k),
                                           "JMP_16_IF_NOT_ZERO $reg, $k", []>;

   def JMP_16_IF_SAME_SIGN : I8085Pseudo<(outs),(ins GR16: $lhs, GR16: $rhs, addressTarget:$k),
                                           "JMP_16_IF_SAME_SIGN $lhs, $rhs, $k", []>;   
   
   def JMP_16_IF_POSITIVE : I8085Pseudo<(outs),(ins GR16: $reg, addressTarget:$k),
                                           "JMP_16_IF_POSITIVE $reg, $k", []>; 
                                                                                 
}

// let Constraints = "$src = $dst" in {
//    def INV : I8085Pseudo<(outs GR8: $dst),
//                         (ins GR8: $src),
//                         "INVERSE $src", []>; 
// }

let Defs=[SREG], Uses=[A] in {
   def CMP : F2<0b10111,(outs),(ins GR8: $rs),"CMP $rs", []>;
}

let Defs=[A], Uses=[A] in
def CMA : F1<0b00101111,(outs),(ins),"CMA", []>;
