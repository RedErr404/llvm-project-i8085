//===-- I8085InstrInfo.td - I8085 Instruction defs -------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the I8085 instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

include "I8085InstrFormats.td"

//===----------------------------------------------------------------------===//
// I8085 Type Profiles
//===----------------------------------------------------------------------===//

def SDT_I8085CallSeqStart : SDCallSeqStart<[SDTCisVT<0, i16>, SDTCisVT<1, i16>]>;
def SDT_I8085CallSeqEnd : SDCallSeqEnd<[SDTCisVT<0, i16>, SDTCisVT<1, i16>]>;
def SDT_I8085Call : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;
def SDT_I8085TCRet : SDTypeProfile<0, 2, [SDTCisPtrTy<0>, SDTCisVT<1, i16>]>;
def SDT_I8085Wrapper : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>, SDTCisPtrTy<0>]>;
def SDT_I8085Brcond
    : SDTypeProfile<0, 2, [SDTCisVT<0, OtherVT>, SDTCisVT<1, i8>]>;
def SDT_I8085Cmp : SDTypeProfile<0, 2, [SDTCisSameAs<0, 1>]>;
def SDT_I8085Tst : SDTypeProfile<0, 1, [SDTCisInt<0>]>;
def SDT_I8085SelectCC
    : SDTypeProfile<1, 3,
                    [SDTCisSameAs<0, 1>, SDTCisSameAs<1, 2>, SDTCisVT<3, i8>]>;

// Type profile for TRUNC32_HI: takes i32, returns i16
def SDT_I8085Trunc32Hi : SDTypeProfile<1, 1, [SDTCisVT<0, i16>, SDTCisVT<1, i32>]>;

//===----------------------------------------------------------------------===//
// I8085 Specific Node Definitions
//===----------------------------------------------------------------------===//

def I8085retflag : SDNode<"I8085ISD::RET_FLAG", SDTNone,
                        [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def I8085retiflag : SDNode<"I8085ISD::RETI_FLAG", SDTNone,
                         [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

def I8085callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_I8085CallSeqStart,
                              [SDNPHasChain, SDNPOutGlue]>;
def I8085callseq_end : SDNode<"ISD::CALLSEQ_END", SDT_I8085CallSeqEnd,
                            [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def I8085call : SDNode<"I8085ISD::CALL", SDT_I8085Call,
                     [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue, SDNPVariadic]>;
def I8085tcret : SDNode<"I8085ISD::TC_RETURN", SDT_I8085TCRet,
                      [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

def I8085Wrapper : SDNode<"I8085ISD::WRAPPER", SDT_I8085Wrapper>;

def I8085brcond
    : SDNode<"I8085ISD::BRCOND", SDT_I8085Brcond, [SDNPHasChain, SDNPInGlue]>;
def I8085cmp : SDNode<"I8085ISD::CMP", SDT_I8085Cmp, [SDNPOutGlue]>;
def I8085cmpc : SDNode<"I8085ISD::CMPC", SDT_I8085Cmp, [SDNPInGlue, SDNPOutGlue]>;
def I8085tst : SDNode<"I8085ISD::TST", SDT_I8085Tst, [SDNPOutGlue]>;
def I8085selectcc : SDNode<"I8085ISD::SELECT_CC", SDT_I8085SelectCC, [SDNPInGlue]>;

// Extract high word from 32-bit value (bytes 2-3)
def I8085trunc32hi : SDNode<"I8085ISD::TRUNC32_HI", SDT_I8085Trunc32Hi>;

// Shift nodes.
def I8085lsl : SDNode<"I8085ISD::LSL", SDTIntUnaryOp>;
def I8085lsr : SDNode<"I8085ISD::LSR", SDTIntUnaryOp>;
def I8085rol : SDNode<"I8085ISD::ROL", SDTIntUnaryOp>;
def I8085ror : SDNode<"I8085ISD::ROR", SDTIntUnaryOp>;
def I8085asr : SDNode<"I8085ISD::ASR", SDTIntUnaryOp>;
def I8085lslhi : SDNode<"I8085ISD::LSLHI", SDTIntUnaryOp>;
def I8085lsrlo : SDNode<"I8085ISD::LSRLO", SDTIntUnaryOp>;
def I8085asrlo : SDNode<"I8085ISD::ASRLO", SDTIntUnaryOp>;
def I8085lslbn : SDNode<"I8085ISD::LSLBN", SDTIntBinOp>;
def I8085lsrbn : SDNode<"I8085ISD::LSRBN", SDTIntBinOp>;
def I8085asrbn : SDNode<"I8085ISD::ASRBN", SDTIntBinOp>;
def I8085lslwn : SDNode<"I8085ISD::LSLWN", SDTIntBinOp>;
def I8085lsrwn : SDNode<"I8085ISD::LSRWN", SDTIntBinOp>;
def I8085asrwn : SDNode<"I8085ISD::ASRWN", SDTIntBinOp>;

// Pseudo shift nodes for non-constant shift amounts.
def I8085lslLoop : SDNode<"I8085ISD::LSLLOOP", SDTIntShiftOp>;
def I8085lsrLoop : SDNode<"I8085ISD::LSRLOOP", SDTIntShiftOp>;
def I8085rolLoop : SDNode<"I8085ISD::ROLLOOP", SDTIntShiftOp>;
def I8085rorLoop : SDNode<"I8085ISD::RORLOOP", SDTIntShiftOp>;
def I8085asrLoop : SDNode<"I8085ISD::ASRLOOP", SDTIntShiftOp>;

class I8085ImmAsmOperand<string AsmName> : AsmOperandClass {
  let Name = AsmName;
  let RenderMethod = "addImmOperands";
  let DiagnosticType = !strconcat("Invalid", AsmName);
}

def I8085Imm8AsmOperand : I8085ImmAsmOperand<"Imm8">;
def I8085Imm16AsmOperand : I8085ImmAsmOperand<"Imm16">;

def I8085Imm8 : Operand<i8>,
                ImmLeaf<i8, [{ return Imm >= -128 && Imm <= 255; }] > {
  let ParserMatchClass = I8085Imm8AsmOperand;
  let EncoderMethod = "encodeImm<I8085::fixup_lo8, 1>";
}

def I8085Imm16 : Operand<i16>,
                 ImmLeaf<i16, [{ return Imm >= -32768 && Imm <= 65535; }] > {
  let ParserMatchClass = I8085Imm16AsmOperand;
  let EncoderMethod = "encodeImm<I8085::fixup_16, 1>";
}


// Address operand for `SP+imm` used by STD{W}SPQRr
def memspi : Operand<iPTR> { 
  let MIOperandInfo = (ops GPRSP, I8085Imm16); 
}

def imm16Addr : Operand<i16> {
  let EncoderMethod = "encodeImm<I8085::fixup_16, 1>";
}



// Addressing mode pattern reg+imm6
def addr : ComplexPattern<iPTR, 2, "SelectAddr", [], [SDNPWantRoot]>;

// AsmOperand class for a pointer register.
// Used with the LD/ST family of instructions.
// See FSTLD in I8085InstrFormats.td
// def PtrRegAsmOperand : AsmOperandClass { let Name = "Reg"; }

//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//
// I8085 Instruction list
//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//

// ADJCALLSTACKDOWN/UP implicitly use/def SP because they may be expanded into
// a stack adjustment and the codegen must know that they may modify the stack
// pointer before prolog-epilog rewriting occurs.
// Pessimistically assume ADJCALLSTACKDOWN / ADJCALLSTACKUP will become
// sub / add which can clobber SREG.
let Defs = [SP, HL, SREG], Uses = [SP] in {
  def ADJCALLSTACKDOWN : I8085Pseudo<(outs),
                                (ins I8085Imm16
                                 : $amt, I8085Imm16
                                 : $amt2),
                                "#ADJCALLSTACKDOWN", [(I8085callseq_start timm
                                                       : $amt, timm
                                                       : $amt2)]>;

  // R31R30 is used to update SP. It is normally free because it is a
  // call-clobbered register but it is necessary to set it as a def as the
  // register allocator might use it in rare cases (for rematerialization, it
  // seems). hasSideEffects needs to be set to true so this instruction isn't
  // considered dead.
  def ADJCALLSTACKUP : I8085Pseudo<(outs),
               (ins I8085Imm16
                : $amt1, I8085Imm16
                : $amt2),
               "#ADJCALLSTACKUP", [(I8085callseq_end timm
                                    : $amt1, timm
                                    : $amt2)]>;
}

//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//
// I8085 Arithmetic Instructions list
//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//

let Constraints = "$src = $rd", Defs=[SREG, A]  in {

  def SUB_8
      : I8085Pseudo<(outs GR8: $rd),
              (ins GR8: $src, GR8: $rr),
              "SUB_8\t$rd, $rr",
              [(set i8: $rd, (sub i8: $src, i8: $rr))]>;

  def SUBI_8
      : I8085Pseudo<(outs GR8: $rd),
              (ins GR8: $src, I8085Imm8: $rr),
              "SUBI_8\t$rd, $rr",
              [(set i8: $rd, (sub i8: $src, imm: $rr))]>;

  def SUB_16
      : I8085Pseudo<(outs GR16: $rd),
              (ins GR16: $src, GR16: $rr),
              "SUB_16\t$rd, $rr",
              [(set i16: $rd, (sub i16: $src, i16: $rr))]> {
    let Constraints = "";
  }

  def ADD_8
      : I8085Pseudo<(outs GR8: $rd),
              (ins GR8: $src, GR8: $rr),
              "ADD_8\t$rd, $rr",
              [(set i8: $rd, (add i8: $src, i8: $rr))]>;

  def ADD_16
      : I8085Pseudo<(outs GR16: $rd),
              (ins GR16: $src, GR16: $rr),
              "ADD_16\t$rd, $rr",
              [(set i16: $rd, (add i16: $src, i16: $rr))]>;
  def SHL_8
      : I8085InserterPseudo<(outs GR8: $rd),
              (ins GR8: $src, GR8: $rr),
              "SHL_8\t$rd, $rr",
              [(set i8: $rd, (shl i8: $src, i8: $rr))]>;
  def SRA_8
      : I8085InserterPseudo<(outs GR8: $rd),
              (ins GR8: $src, GR8: $rr),
              "SRA_8\t$rd, $rr",
              [(set i8: $rd, (sra i8: $src, i8: $rr))]>;
  def SRL_8
      : I8085InserterPseudo<(outs GR8: $rd),
              (ins GR8: $src, GR8: $rr),
              "SRL_8\t$rd, $rr",
              [(set i8: $rd, (srl i8: $src, i8: $rr))]>;

  def SHL_16
      : I8085InserterPseudo<(outs GR16: $rd),
              (ins GR16: $src, GR8: $rr),
              "SHL_16\t$rd, $rr",
              [(set i16: $rd, (shl i16: $src, i8: $rr))]>;

  def SRA_16
      : I8085InserterPseudo<(outs GR16: $rd),
              (ins GR16: $src, GR8: $rr),
              "SRA_16\t$rd, $rr",
              [(set i16: $rd, (sra i16: $src, i8: $rr))]>;            
  def SRL_16
      : I8085InserterPseudo<(outs GR16: $rd),
              (ins GR16: $src, GR8: $rr),
              "SRL_16\t$rd, $rr",
              [(set i16: $rd, (srl i16: $src, i8: $rr))]>;            

}

def SELECT_8
    : I8085InserterPseudo<(outs GR8: $rd),
            (ins GR8: $cond, GR8: $tval, GR8: $fval),
            "SELECT_8\t$rd, $cond, $tval, $fval",
            []>;

def SELECT_16
    : I8085InserterPseudo<(outs GR16: $rd),
            (ins GR8: $cond, GR16: $tval, GR16: $fval),
            "SELECT_16\t$rd, $cond, $tval, $fval",
            []>;

let Defs=[SREG,A], Uses=[A] in def SUB : F2<0b10010,(outs), (ins GR8: $rs), "SUB $rs", []>;
let Defs=[SREG,A], Uses=[A,SREG] in def SBB : F2<0b10011,(outs), (ins GR8: $rs), "SBB $rs",[]>;
let Uses=[H,L,A],Defs=[SREG,A] in def SUB_M : F1<0b10010110,(outs), (ins), "SUB M", []>;
let Uses=[H,L,A,SREG],Defs=[SREG,A] in def SBB_M : F1<0b10011110,(outs), (ins), "SBB M", []>;
let Defs=[SREG,A], Uses=[A] in def ADD : F2<0b10000,(outs), (ins GR8: $rs), "ADD $rs", []>;
let Defs=[SREG,A], Uses=[A,SREG] in def ADC : F2<0b10001,(outs), (ins GR8: $rs), "ADC $rs", []>;

let Uses=[H,L,A],Defs=[SREG,A] in def ADD_M : F1<0b10000110,(outs), (ins), "ADD M", []>;
let Uses=[H,L,A,SREG],Defs=[SREG,A] in def ADC_M : F1<0b10001110,(outs), (ins), "ADC M", []>;
let Uses=[H,L,A],Defs=[SREG,A] in def ANA_M : F1<0b10100110,(outs), (ins), "ANA M", []>;
let Uses=[H,L,A],Defs=[SREG,A] in def ORA_M : F1<0b10110110,(outs), (ins), "ORA M", []>;


let Uses=[H,L,A],Defs=[SREG] in def CMP_M : F1<0b10111110,(outs), (ins), "CMP M", []>;
let Uses=[H,L,A],Defs=[SREG,A] in def XRA_M : F1<0b10101110,(outs), (ins), "XRA M", []>;

let Defs=[SREG,A], Uses=[A] in def ADI : F5<0b11000110,(outs), (ins I8085Imm8: $k), "ADI $k", []>;
let Defs=[SREG,A], Uses=[A,SREG] in def ACI : F5<0b11001110,(outs), (ins I8085Imm8: $k), "ACI $k", []>;
let Defs=[SREG,A], Uses=[A,SREG] in def SBI : F5<0b11011110,(outs), (ins I8085Imm8: $k), "SBI $k", []>;
let Defs=[SREG,A], Uses=[A] in def SUI : F5<0b11010110,(outs), (ins I8085Imm8: $k), "SUI $k", []>;

let Constraints = "$rs = $rd", Defs=[SREG]  in {

def DCR : F3<0b1,(outs GR8: $rd), (ins GR8: $rs), "DCR $rs", []>;

def INR : F3<0b0,(outs GR8: $rd), (ins GR8: $rs), "INR $rs", []>;

}

// Memory-form increment/decrement: read-modify-write the byte at (HL) in a
// single 1-byte instruction. Like the register INR/DCR they set Z,S,P,AC and
// PRESERVE CY. Opcodes 0x34 / 0x35.
let Uses=[H,L], Defs=[SREG], mayLoad = 1, mayStore = 1 in {
def INR_M : F1<0b00110100,(outs),(ins),"INR M",[]>;
def DCR_M : F1<0b00110101,(outs),(ins),"DCR M",[]>;
}

//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//
// I8085 Logical Instructions list
//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//


let Constraints = "$src = $rd", Defs=[SREG, A]  in {

  def AND_8
      : I8085Pseudo<(outs GR8: $rd),
              (ins GR8: $src, GR8: $rr),
              "AND_8\t$rd, $rr",
              [(set i8: $rd, (and i8: $src, i8: $rr))]>;

  def AND_16
      : I8085Pseudo<(outs GR16: $rd),
              (ins GR16: $src, GR16: $rr),
              "AND_16\t$rd, $rr",
              [(set i16: $rd, (and i16: $src, i16: $rr))]>;

  def OR_8
      : I8085Pseudo<(outs GR8: $rd),
              (ins GR8: $src, GR8: $rr),
              "OR_8\t$rd, $rr",
              [(set i8: $rd, (or i8: $src, i8: $rr))]>;

  def OR_16
      : I8085Pseudo<(outs GR16: $rd),
              (ins GR16: $src, GR16: $rr),
              "OR_16\t$rd, $rr",
              [(set i16: $rd, (or i16: $src, i16: $rr))]>;

  def XOR_8
      : I8085Pseudo<(outs GR8: $rd),
              (ins GR8: $src, GR8: $rr),
              "XOR_8\t$rd, $rr",
              [(set i8: $rd, (xor i8: $src, i8: $rr))]>;

  def XOR_16
      : I8085Pseudo<(outs GR16: $rd),
              (ins GR16: $src, GR16: $rr),
              "XOR_16\t$rd, $rr",
              [(set i16: $rd, (xor i16: $src, i16: $rr))]>;

  def ANDI_8
      : I8085Pseudo<(outs GR8: $rd),
              (ins GR8: $src, I8085Imm8: $val),
              "ANDI_8\t$rd, $val",
              [(set i8: $rd, (and i8: $src, imm: $val))]>;
  def ORI_8
      : I8085Pseudo<(outs GR8: $rd),
              (ins GR8: $src, I8085Imm8: $val),
              "ORI_8\t$rd, $val",
              [(set i8: $rd, (or i8: $src, imm: $val))]>;
  def XORI_8
      : I8085Pseudo<(outs GR8: $rd),
              (ins GR8: $src, I8085Imm8: $val),
              "XORI_8\t$rd, $val",
              [(set i8: $rd, (xor i8: $src, imm: $val))]>;
}

// ~x for i16. Matching (not i16) directly avoids materializing an all-ones
// register (LXI D, -1) and lets the expansion use per-byte CMA (1 byte, 4T,
// flag-preserving). CMA does not touch flags, so only A is clobbered.
let Constraints = "$src = $rd", Defs=[A] in
def NOT_16
    : I8085Pseudo<(outs GR16: $rd),
            (ins GR16: $src),
            "NOT_16\t$rd",
            [(set i16: $rd, (not i16: $src))]>;

let Defs=[SREG,A], Uses=[A] in def ANA : F2<0b10100,(outs), (ins GR8: $rs), "ANA $rs", []>;
let Defs=[SREG,A], Uses=[A] in def ORA : F2<0b10110,(outs), (ins GR8: $rs), "ORA $rs", []>;
let Defs=[SREG,A], Uses=[A] in def XRA : F2<0b10101,(outs), (ins GR8: $rs), "XRA $rs", []>;

let Defs=[SREG,A], Uses=[A] in def ANI : F5<0b11100110,(outs), (ins I8085Imm8: $k), "ANI $k", []>;
let Defs=[SREG,A], Uses=[A] in def ORI : F5<0b11110110,(outs), (ins I8085Imm8: $k), "ORI $k", []>;
let Defs=[SREG,A], Uses=[A] in def XRI : F5<0b11101110,(outs), (ins I8085Imm8: $k), "XRI $k", []>;
let Defs=[SREG], Uses=[A] in def CPI : F5<0b11111110,(outs), (ins I8085Imm8: $k), "CPI $k", []>;

let hasSideEffects = 1, Defs=[A] in
def IN : F5<0b11011011,(outs), (ins I8085Imm8: $k), "IN $k", []>;
let hasSideEffects = 1, Uses=[A] in
def OUT : F5<0b11010011,(outs), (ins I8085Imm8: $k), "OUT $k", []>;


//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//
// I8085 Bitwise Instructions list
//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//




let Constraints = "$rs = $rd", Defs=[SREG]  in {
  let Defs=[SREG,A] in def RL_8 : I8085Pseudo<(outs GR8: $rd), (ins GR8: $rs), "RL_8 $rs", []>;
  let Defs=[SREG,A] in def RR_8 : I8085Pseudo<(outs GR8: $rd), (ins GR8: $rs), "RR_8 $rs", []>;
  let Defs=[SREG,A] in def ASR_8 : I8085Pseudo<(outs GR8: $rd), (ins GR8: $rs), "ASR_8 $rs", []>;

  let Defs=[SREG,A] in def RL_16 : I8085Pseudo<(outs GR16: $rd), (ins GR16: $rs), "RL_16 $rs", []>;
  let Defs=[SREG,A] in def RR_16 : I8085Pseudo<(outs GR16: $rd), (ins GR16: $rs), "RR_16 $rs", []>;
  let Defs=[SREG,A] in def ASR_16 : I8085Pseudo<(outs GR16: $rd), (ins GR16: $rs), "ASR_16 $rs", []>;
}

// i16 byte-shuffle shift pseudos (shift by 8 via register moves)
let Defs=[SREG, A] in def SHL_16_BY_8  : I8085Pseudo<(outs GR16: $rd), (ins GR16: $rs), "SHL_16_BY_8 $rs", []>;
let Defs=[SREG, A] in def SRL_16_BY_8  : I8085Pseudo<(outs GR16: $rd), (ins GR16: $rs), "SRL_16_BY_8 $rs", []>;
let Defs=[SREG, A] in def ASR_16_BY_8  : I8085Pseudo<(outs GR16: $rd), (ins GR16: $rs), "ASR_16_BY_8 $rs", []>;

// Sign extension: ashr i16, 15 → 0x0000 or 0xFFFF based on sign bit
let Defs=[SREG, A] in def SIGN_EXTEND_16 : I8085Pseudo<(outs GR16: $rd), (ins GR16: $rs), "SIGN_EXTEND_16 $rs", []>;

// Constant multiply strength reduction pseudo.
// Multiplies a GR16 value by an immediate constant using DAD H (shift left),
// DAD B (add saved value), and 16-bit subtract sequences.
// All computation happens in HL with BC as the save register.
// The expansion is generated by I8085ExpandPseudoInsts based on the immediate.
// Clobbers: HL (accumulator), BC (save register), A (for sub), SREG (flags).
let Defs=[SREG, A, HL, BC, H, L, B, C] in
def MUL_16_IMM : I8085Pseudo<(outs GR16: $rd), (ins GR16: $rs, i16imm: $imm),
                              "MUL_16_IMM $rs, $imm", []>;

let Defs=[SREG,A], Uses=[A] in def RLC : F1<0b00000111,(outs), (ins), "RLC", []>;
let Defs=[SREG,A], Uses=[A] in def RRC : F1<0b00001111,(outs), (ins), "RRC", []>;

let Defs=[SREG,A], Uses=[A,SREG] in def RAL : F1<0b00010111,(outs), (ins), "RAL", []>;
let Defs=[SREG,A], Uses=[A,SREG] in def RAR : F1<0b00011111,(outs), (ins), "RAR", []>;

def NOP : F1<0b00000000,(outs), (ins), "NOP", []>;
let hasSideEffects = 1, isBarrier = 1 in
def HLT : F1<0b01110110,(outs), (ins), "HLT", []>;

let Defs=[SREG,A], Uses=[A,SREG] in def DAA : F1<0b00100111,(outs), (ins), "DAA", []>;
let Defs=[SREG] in def STC : F1<0b00110111,(outs), (ins), "STC", []>;
let Defs=[SREG], Uses=[SREG] in def CMC : F1<0b00111111,(outs), (ins), "CMC", []>;

let hasSideEffects = 1 in def DI : F1<0b11110011,(outs), (ins), "DI", []>;
let hasSideEffects = 1 in def EI : F1<0b11111011,(outs), (ins), "EI", []>;
let hasSideEffects = 1, Defs=[A] in def RIM : F1<0b00100000,(outs), (ins), "RIM", []>;
let hasSideEffects = 1, Uses=[A] in def SIM : F1<0b00110000,(outs), (ins), "SIM", []>;

let Defs=[D,E,H,L], Uses=[D,E,H,L] in
def XCHG : F1<0b11101011,(outs), (ins), "XCHG", []>;
let Defs=[H,L], Uses=[H,L,SP], hasSideEffects = 1, mayLoad = 1, mayStore = 1 in
def XTHL : F1<0b11100011,(outs), (ins), "XTHL", []>;
let Uses=[H,L], isBranch = 1, isBarrier = 1, isTerminator = 1,
    isIndirectBranch = 1 in
def PCHL : F1<0b11101001,(outs), (ins), "PCHL", []>;



//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//
// I8085 Data movement operations
//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//


def MVI : FMVI<(outs GR8: $rd),
                    (ins I8085Imm8: $k),
                    "MVI $rd, $k", [(set i8: $rd, imm: $k)]>;


def MOV : FMOV<(outs GR8: $rd),
                    (ins GR8: $rs),
                    "MOV $rd, $rs", []>;


def mspi : Operand<iPTR> { 
  let MIOperandInfo = (ops GR16, I8085Imm16); 
}

// Addressing mode for stores that must avoid HL as a base (allow BC/DE/SP).
def mspibdsp : Operand<iPTR> {
  let MIOperandInfo = (ops GR16BDSP, I8085Imm16);
}


def LXI : F4<0b0001,(outs GR16: $rd), (ins I8085Imm16: $k), "LXI $rd, $k", []>;
def INX : F8<0b0011,(outs GR16: $rd), (ins), "INX $rd", []>;
def DCX : F8<0b1011,(outs GR16: $rd), (ins), "DCX $rd", []>;

// 16-bit +/-1 as a single INX/DCX (1 byte, no flags) instead of the byte-wise
// ADD_16/SUB_16 sequence. The value stays in its own pair (any of BC/DE/HL/SP),
// so no register shuffling is needed. INX/DCX affect no flags, matching the
// add/sub DAG node which produces only the value.
let Constraints = "$src = $rd" in {
  def INC16 : I8085Pseudo<(outs GR16: $rd), (ins GR16: $src), "INC16\t$rd", []>;
  def DEC16 : I8085Pseudo<(outs GR16: $rd), (ins GR16: $src), "DEC16\t$rd", []>;
}

let Uses=[A] in def STAX : F8<0b0010,(outs), (ins GR16BD: $rd), "STAX $rd", []>;
let Defs=[A] in def LDAX : F8<0b1010,(outs), (ins GR16BD: $rd), "LDAX $rd", []>;

let Uses = [H,L] in {
  let Defs=[SREG,H,L] in def DAD : F7<(outs), (ins GR16: $rs), "DAD $rs", []>;

  let Defs=[SP] in def SPHL : F1<0b11111001,(outs), (ins), "SPHL", []>;

  let Uses=[H,L] in def SHLD : F6<0b00100010,(outs), (ins imm16Addr: $k), "SHLD $k", []>;

  let Uses=[H,L] in def MOV_M  : FMOV_T_M<(outs), (ins GR8: $rs), "MOV M, $rs", []>;

  let Uses=[H,L] in def MOV_FROM_M  : FMOV_F_M<(outs GR8: $rd), (ins), "MOV $rd, M", []>;

  let Uses=[H,L] in
  def MVI_M : F5<0b00110110,(outs), (ins I8085Imm8: $k), "MVI M, $k", []>;
}          

// LHLD writes HL from memory; it does NOT read HL, so it is defined outside the
// `let Uses=[H,L]` block above (which would otherwise mark H/L as used and trip
// the verifier with "using an undefined physical register").
let Defs=[H,L] in def LHLD : F6<0b00101010,(outs), (ins imm16Addr: $k), "LHLD $k", []>;

let Uses=[A] in def STA : F6<0b00110010,(outs), (ins imm16Addr: $k), "STA $k", []>;
let Defs=[A] in def LDA : F6<0b00111010,(outs), (ins imm16Addr: $k), "LDA $k", []>;

let Defs=[SP, HL, SREG], Uses=[SP] in {

def GROW_STACK_BY 
    : I8085Pseudo<(outs),
            (ins I8085Imm16: $amount),
            "GROW_STACK_BY $amount",
            []>;

def SHRINK_STACK_BY 
    : I8085Pseudo<(outs),
            (ins I8085Imm16: $amount),
            "SHRINK_STACK_BY $amount",
            []>;        
}       

let Uses=[SP] in {
def LOAD_16 : I8085Pseudo<(outs GR16: $dst),
                       (ins I8085Imm16: $src),
                       "LOAD_16 $dst, $src", [(set i16
                                             : $dst, imm
                                             : $src)]>; 

// NOTE: These pseudos use HL as a scratch register internally, but they
// conditionally preserve HL via PUSH/POP when isHLOrSubRegLive() returns true
// during expansion.  We intentionally do NOT declare HL in Defs so that
// post-RA passes (especially Machine Copy Propagation) see HL as preserved
// across these instructions.  If the register allocator assigns a value to HL
// that is live across one of these pseudos, the expansion will detect this
// and emit PUSH/POP to save and restore HL.
//
// SREG is NOT declared in Defs.  Although the expansion may use DAD SP to
// compute the stack address (and DAD clobbers carry), we handle this at
// expansion time: isPhysRegLive(SREG) detects when flags are live across the
// pseudo and emits PUSH PSW / POP PSW around the DAD.  Declaring Defs=[SREG]
// here would make the register allocator treat each pseudo as a legitimate
// SREG definition, breaking liveness: if three consecutive spills sit between
// SUB and JC, only the last one would see SREG as live (the earlier ones
// "kill" it), leaving the first two DADs unprotected.
def STORE_16 : I8085Pseudo<(outs),
                            (ins mspibdsp: $dst, GR16: $src),
                            "STORE_16 $dst, $src", [(store i16: $src, addr: $dst)]>;

def STORE_8 : I8085Pseudo<(outs),
                        (ins mspibdsp: $memri,
                         GR8NoHL: $reg),
                        "STORE $memri, $reg", [(store i8: $reg, addr: $memri)]>;

// LOAD_16_WITH_ADDR: A is NOT declared as clobbered because most expansion
// paths (dest=BC/DE) do not touch A. When dest=HL, the expansion uses A as a
// temp but handles preservation via shouldPreservePSW/pushPSW. Declaring
// Defs=[A] here caused MCP to incorrectly remove $a = COPY $b as dead.
def LOAD_16_WITH_ADDR : I8085Pseudo<(outs GR16: $dst),
                       (ins mspi: $src),
                       "LOAD $dst, $src", [(set i16: $dst, (load addr: $src))]>;

def LOAD_8_WITH_ADDR : I8085Pseudo<(outs GR8: $dst),
                       (ins mspi: $src),
                       "LOAD $dst, $src", [(set i8: $dst, (load addr: $src))]>;


def STORE_8_AT_OFFSET_WITH_SP : I8085Pseudo<(outs),
                       (ins GR8NoHL: $src, I8085Imm16: $offset),
                       "STORE_8_SP_AT_OFFSET\t$offset, $src", [(store i8: $src, imm: $offset)]>;

def STORE_16_AT_OFFSET_WITH_SP : I8085Pseudo<(outs),
                       (ins GR16: $src, I8085Imm16: $offset),
                       "STORE_16_SP_AT_OFFSET\t$offset, $src", [(store i16: $src, imm: $offset)]>;                       

}       

let Uses = [SP], Defs = [SP] in {
  def POP : F10<0b0001,(outs GR16PP: $rd), (ins), "POP $rd", []>;
  def PUSH : F10<0b0101,(outs), (ins GR16PP: $rd), "PUSH $rd", []>;
}

let Uses=[SP] in {

let Defs=[HL] in def LOAD_8_WITH_IMM_ADDR : I8085Pseudo<(outs GR8: $dst),
                       (ins imm16Addr: $src),
                       "LOAD_8_ADDR $dst, $src", []>;  

let Defs=[HL, A] in def LOAD_16_WITH_IMM_ADDR : I8085Pseudo<(outs GR16: $dst),
                       (ins imm16Addr: $src),
                       "LOAD_16_ADDR $dst, $src", []>;

let Defs=[HL, A] in def LOAD_8_ADDR_CONTENT : I8085Pseudo<(outs GR8: $dst),
                       (ins GR16: $src),
                       "LOAD_8_ADDR_CONTENT $dst, $src", [(set i8: $dst, (load i16: $src))]>;

// LOAD_16_ADDR_CONTENT: Load 16-bit value from address in register.
// Output constrained to GR16BD (BC/DE only) because HL is always clobbered
// during expansion (used as address pointer). This avoids a Machine Copy
// Propagation bug where "implicit-def dead $hl" causes incorrect COPY removal.
//
// NOTE: Allowing HL as output would require removing HL from Defs (risky for
// liveness) or custom instruction selection to pick a variant without HL in Defs.
// The expansion code can handle HL output, but register allocation needs to
// know HL is clobbered, which prevents it from being allocated as output anyway.
let Defs=[HL, A] in def LOAD_16_ADDR_CONTENT : I8085Pseudo<(outs GR16BD: $dst),
                       (ins GR16: $src),
                       "LOAD_16_ADDR_CONTENT $dst, $src", [(set i16: $dst, (load i16: $src))]>;

let Defs=[HL, A] in def STORE_8_ADDR_CONTENT : I8085Pseudo<(outs),
                       (ins GR16: $addr, GR8NoHL: $src),
                       "STORE_8_ADDR_CONTENT $addr, $src",
                       [(store i8: $src, i16: $addr)]>;

let Defs=[HL, A] in def STORE_16_ADDR_CONTENT : I8085Pseudo<(outs),
                       (ins GR16: $addr, GR16BD: $src),
                       "STORE_16_ADDR_CONTENT $addr, $src",
                       [(store i16: $src, i16: $addr)]>;

let Defs=[HL], mayStore = 1 in def STORE_8_WITH_IMM_ADDR : I8085Pseudo<(outs),
                       (ins imm16Addr: $addr, GR8NoHL: $src),
                       "STORE_8_ADDR $addr, $src", []>;

let Defs=[HL], mayStore = 1 in def STORE_16_WITH_IMM_ADDR : I8085Pseudo<(outs),
                       (ins imm16Addr: $addr, GR16BD: $src),
                       "STORE_16_ADDR $addr, $src", []>;
}

// In-place ++/-- of an i8 at a fixed (global/immediate) address. Expands to
// `LXI H, addr ; INR M|DCR M`. Defs HL (address pointer) and SREG (INR/DCR
// clobber Z,S,P,AC).
let Defs=[HL, SREG], mayLoad = 1, mayStore = 1 in {
def INC_MEM_8_WITH_IMM_ADDR : I8085Pseudo<(outs), (ins imm16Addr: $addr),
                       "INC_MEM_8_ADDR $addr", []>;
def DEC_MEM_8_WITH_IMM_ADDR : I8085Pseudo<(outs), (ins imm16Addr: $addr),
                       "DEC_MEM_8_ADDR $addr", []>;
}

// Same in-place ++/-- but for a byte at an address held in a GR16 register
// (e.g. (*p)++). The pointer is moved into HL (INR M/DCR M only address via HL)
// and then incremented/decremented in place.
let Defs=[HL, SREG], mayLoad = 1, mayStore = 1 in {
def INC_MEM_8_ADDR_CONTENT : I8085Pseudo<(outs), (ins GR16: $addr),
                       "INC_MEM_8_CONTENT $addr", []>;
def DEC_MEM_8_ADDR_CONTENT : I8085Pseudo<(outs), (ins GR16: $addr),
                       "DEC_MEM_8_CONTENT $addr", []>;
}

// Branchless i8 constant shift by a large amount k (5..7). A shift by k is a
// circular rotate the *short* way (8-k positions) followed by masking off the
// bits that wrapped in. rd = result, rs = source, n = shift amount. Clobber A
// (scratch) and SREG (rotate/ANI set flags).
let Defs=[SREG, A] in {
def SHL_8_HI : I8085Pseudo<(outs GR8:$rd), (ins GR8:$rs, I8085Imm8:$n),
                       "SHL_8_HI $rd, $rs, $n", []>;
def SRL_8_HI : I8085Pseudo<(outs GR8:$rd), (ins GR8:$rs, I8085Imm8:$n),
                       "SRL_8_HI $rd, $rs, $n", []>;
// ashr i8 by 7 = smear the sign bit across all 8 bits (0x00 / 0xFF). Expands to
// MOV A,rs; ADD A (CY = bit7); SBB A (A = -CY); MOV rd,A.
def ASR_8_BY_7 : I8085Pseudo<(outs GR8:$rd), (ins GR8:$rs),
                       "ASR_8_BY_7 $rd, $rs", []>;
}


def addressTarget : Operand<OtherVT> {
  // let PrintMethod = "printPCRelImm";
  let EncoderMethod = "encodeCallTarget<I8085::fixup_16, 1>";
}

let isCall = 1 in {
let Uses = [SP], Defs = [SP, A, B, C, D, E, H, L] in def CALL : F6<0b11001101,(outs), (ins imm16Addr:$k),
                                       "CALL $k", [(I8085call imm:$k)]>;
}

let isCall = 1 in {
let Uses = [SP, HL], Defs = [SP, A, B, C, D, E, H, L] in def CALL_INDIRECT : I8085Pseudo<(outs), (ins),
                                       "CALL_INDIRECT", []>;
}

let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [SP] in {
  def TCRETURN : I8085Pseudo<(outs), (ins imm16Addr:$dst, I8085Imm16:$adj),
                             "TCRETURN $dst", []>;
}

// Indirect tail call: jump to the target held in BC via PCHL (no return address
// pushed). BC is used because the epilogue clobbers HL but not BC; the target is
// moved BC->HL and PCHL'd only after the epilogue (done at asm-print time).
let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1,
    Uses = [SP, BC], Defs = [HL] in {
  def TCRETURN_INDIRECT : I8085Pseudo<(outs), (ins), "TCRETURN_INDIRECT", []>;
}

let isTerminator = 1, isReturn = 1, isBarrier = 1 in {
  let Uses = [SP], Defs = [SP] in {
    def RET : F1<0b11001001, (outs), (ins), "RET", [(I8085retflag)]>;
    def RETI : F1<0b11011001, (outs), (ins), "RETI", [(I8085retiflag)]>;
  }
}

def RST : FRST<(outs), (ins I8085Imm8: $k), "RST $k", []>;

def : Pat<(I8085call(i16 tglobaladdr:$dst)), (CALL tglobaladdr:$dst)>;
def : Pat<(I8085call(i16 texternalsym:$dst)), (CALL texternalsym:$dst)>;
def : Pat<(I8085tcret (i16 tglobaladdr:$dst), imm:$adj),
          (TCRETURN tglobaladdr:$dst, imm:$adj)>;
def : Pat<(I8085tcret (i16 texternalsym:$dst), imm:$adj),
          (TCRETURN texternalsym:$dst, imm:$adj)>;



def imm16_Neg_Form : SDNodeXForm<imm, [{
                    return CurDAG->getTargetConstant(-N->getAPIntValue(),
                                                     SDLoc(N), MVT::i16);
                  }]>;


def imm8_Neg_Form : SDNodeXForm<imm, [{
                    return CurDAG->getTargetConstant(-N->getAPIntValue(),
                                                     SDLoc(N), MVT::i8);
                  }]>;


def IsNeg8 : ImmLeaf<i8, [{
  return Imm < 0;
}], imm8_Neg_Form>;

def IsNeg16 : ImmLeaf<i16, [{
  return Imm < 0;
}], imm16_Neg_Form>;


def : Pat<(add i8: $src1, (IsNeg8 :$src2)),
          (SUBI_8 i8: $src1, (imm8_Neg_Form imm: $src2))>;
def : Pat<(add i16: $src1, (IsNeg16 :$src2)),(SUB_16 i16: $src1, (LOAD_16 (imm16_Neg_Form imm: $src2)))>;

// +/-1 map to single-byte INX/DCX (more specific than the general imm and
// IsNeg16 patterns above, so give them higher complexity to win selection).
let AddedComplexity = 20 in {
  def : Pat<(add i16: $src, 1),  (INC16 i16: $src)>;
  def : Pat<(add i16: $src, -1), (DEC16 i16: $src)>;
  def : Pat<(sub i16: $src, 1),  (DEC16 i16: $src)>;
}

def : Pat<(add i8: $src1, (imm :$src2)),(ADD_8 i8: $src1, (MVI imm: $src2))>;
def : Pat<(add i16: $src1, (imm :$src2)),(ADD_16 i16: $src1, (LOAD_16 imm: $src2))>;

def : Pat<(sub i8: $src1, (imm :$src2)), (SUBI_8 i8: $src1, imm: $src2)>;


let Defs=[SREG, A] in def SEXT8TO16
    : I8085Pseudo<(outs GR16
                       : $dst),
                      (ins GR8
                       : $src),
                      "sext8to16 $dst, $src",
                      [(set i16
                        : $dst, (sext i8
                                 : $src))]>;

let Defs=[A] in def ZEXT8TO16
    : I8085Pseudo<(outs GR16
                       : $dst),
                      (ins GR8
                       : $src),
                      "zext8to16 $dst, $src",
                      [(set i16
                        : $dst, (zext i8
                                 : $src))]>;

let Defs=[A] in def AEXT8TO16
    : I8085Pseudo<(outs GR16
                       : $dst),
                      (ins GR8
                       : $src),
                      "aext8to16 $dst, $src",
                      [(set i16
                        : $dst, (anyext i8
                                 : $src))]>;   


def TRUNC16TO8
    : I8085Pseudo<(outs GR8: $dst),
                      (ins GR16: $src),
                      "trunc16to8 $dst, $src",
                      [(set i8
                        : $dst, (trunc i16
                                 : $src))]>;                                 


def : Pat<(i16(load(I8085Wrapper tglobaladdr
                    : $dst))),
          (LOAD_16_WITH_IMM_ADDR tglobaladdr
           : $dst)>;

def : Pat<(i8(load(I8085Wrapper tglobaladdr
                   : $dst))),
          (LOAD_8_WITH_IMM_ADDR tglobaladdr
           : $dst)>;

def : Pat<(i16(I8085Wrapper tglobaladdr : $dst)),
          (LOAD_16 tglobaladdr : $dst)>;

def : Pat<(i16(I8085Wrapper tblockaddress : $dst)),
          (LOAD_16 tblockaddress : $dst)>;

def : Pat<(i16(load(I8085Wrapper tjumptable
                    : $dst))),
          (LOAD_16_WITH_IMM_ADDR tjumptable
           : $dst)>;

def : Pat<(i8(load(I8085Wrapper tjumptable
                   : $dst))),
          (LOAD_8_WITH_IMM_ADDR tjumptable
           : $dst)>;

def : Pat<(i16(I8085Wrapper tjumptable : $dst)),
          (LOAD_16 tjumptable : $dst)>;

def : Pat<(i16(load(I8085Wrapper tconstpool
                    : $dst))),
          (LOAD_16_WITH_IMM_ADDR tconstpool
           : $dst)>;

def : Pat<(i8(load(I8085Wrapper tconstpool
                   : $dst))),
          (LOAD_8_WITH_IMM_ADDR tconstpool
           : $dst)>;

def : Pat<(i16(I8085Wrapper tconstpool : $dst)),
          (LOAD_16 tconstpool : $dst)>;


let Defs = [SREG, H, L],
    hasSideEffects = 0 in  def FRMIDX : I8085Pseudo<(outs GR16: $dst),
                                                    (ins mspi: $memri),
                                                    "FRMIDX\t$dst, $memri", []>;

// Byte swap pseudo-instructions
// BSWAP16 swaps the two bytes of an i16 value
let Defs = [A] in
def BSWAP16 : I8085Pseudo<(outs GR16: $dst),
                          (ins GR16: $src),
                          "BSWAP16 $dst, $src",
                          [(set i16: $dst, (bswap i16: $src))]>;

include "I8085CondBrInstrInfo.td"

include "I8085InstrInfo32.td"
