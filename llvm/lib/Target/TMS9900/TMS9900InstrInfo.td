//===-- TMS9900InstrInfo.td - TMS9900 Instruction defs -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// TMS9900 Instruction Definitions
//
// This file defines the instruction patterns for the TMS9900.
// Initially focusing on register-to-register operations for simplicity.
//
//===----------------------------------------------------------------------===//

include "TMS9900InstrFormats.td"

//===----------------------------------------------------------------------===//
// Type Profiles
//===----------------------------------------------------------------------===//

def SDT_TMS9900Call : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;
def SDT_TMS9900Ret  : SDTypeProfile<0, 0, []>;
def SDT_TMS9900Wrapper : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>, SDTCisPtrTy<0>]>;

// Call sequence start and end
def SDT_TMS9900CallSeqStart : SDCallSeqStart<[SDTCisVT<0, i16>, SDTCisVT<1, i16>]>;
def SDT_TMS9900CallSeqEnd   : SDCallSeqEnd<[SDTCisVT<0, i16>, SDTCisVT<1, i16>]>;

//===----------------------------------------------------------------------===//
// TMS9900 Specific DAG Nodes
//===----------------------------------------------------------------------===//

def TMS9900ret : SDNode<"TMS9900ISD::RET", SDT_TMS9900Ret,
                        [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

def TMS9900reti : SDNode<"TMS9900ISD::RETI", SDT_TMS9900Ret,
                         [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

def TMS9900call : SDNode<"TMS9900ISD::CALL", SDT_TMS9900Call,
                         [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue, SDNPVariadic]>;

// Call sequence markers - used for stack adjustment around calls
def TMS9900callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_TMS9900CallSeqStart,
                                   [SDNPHasChain, SDNPOutGlue]>;
def TMS9900callseq_end   : SDNode<"ISD::CALLSEQ_END", SDT_TMS9900CallSeqEnd,
                                   [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def TMS9900Wrapper : SDNode<"TMS9900ISD::Wrapper", SDT_TMS9900Wrapper>;

// Compare node (sets status register)
def TMS9900cmp : SDNode<"TMS9900ISD::CMP", SDTypeProfile<0, 2, [SDTCisSameAs<0, 1>]>,
                        [SDNPOutGlue]>;

// Conditional branch node
def TMS9900brcc : SDNode<"TMS9900ISD::BR_CC", SDTypeProfile<0, 2, []>,
                         [SDNPHasChain, SDNPInGlue]>;

// Multiply node (16x16->32)
def TMS9900mpy : SDNode<"TMS9900ISD::MPY", SDTypeProfile<1, 2, [SDTCisVT<0, i32>,
                                                                SDTCisVT<1, i16>,
                                                                SDTCisVT<2, i16>]>>;

// Select conditional node
// (TMS9900selectcc trueval, falseval, condcode, lhs, rhs)
// Returns trueval if (lhs condcode rhs), else falseval
def TMS9900selectcc : SDNode<"TMS9900ISD::SELECT_CC",
                             SDTypeProfile<1, 5, [SDTCisSameAs<0, 1>,
                                                  SDTCisSameAs<1, 2>,
                                                  SDTCisVT<3, i16>,
                                                  SDTCisSameAs<4, 5>]>,
                             [SDNPInGlue]>;

// Byte store node - stores HIGH byte of i16 register to memory
// TMS9900 MOVB uses the HIGH byte position, but LLVM uses LOW byte,
// so we shift the value before creating this node.
// (TMS9900bytestore chain, value, ptr)
def TMS9900bytestore : SDNode<"TMS9900ISD::BYTE_STORE",
                              SDTypeProfile<0, 2, [SDTCisVT<0, i16>, SDTCisPtrTy<1>]>,
                              [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;

// Byte load node - loads byte into HIGH byte position of i16 register
// TMS9900 MOVB loads into HIGH byte. After matching this to MOVB,
// we shift right by 8 to move byte to LOW position.
// (TMS9900byteload chain, ptr) -> (value, chain)
def TMS9900byteload : SDNode<"TMS9900ISD::BYTE_LOAD",
                             SDTypeProfile<1, 1, [SDTCisVT<0, i16>, SDTCisPtrTy<1>]>,
                             [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;

//===----------------------------------------------------------------------===//
// AsmOperandClass Definitions (for AsmParser)
//===----------------------------------------------------------------------===//

// Immediate operand class
def TMS9900ImmAsmOperand : AsmOperandClass {
  let Name = "TMS9900Imm";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidImm";
}

// Memory operand class (symbolic: @LABEL or indexed: @offset(Rn))
def TMS9900MemAsmOperand : AsmOperandClass {
  let Name = "TMS9900Mem";
  let RenderMethod = "addMemOperands";
  let ParserMethod = "parseMemOperand";
}

// Indirect register operand class (*Rn)
def TMS9900IndRegAsmOperand : AsmOperandClass {
  let Name = "TMS9900IndReg";
  let RenderMethod = "addIndRegOperands";
  let ParserMethod = "parseIndirectOperand";
}

// Post-increment indirect operand class (*Rn+)
def TMS9900PostIndRegAsmOperand : AsmOperandClass {
  let Name = "TMS9900PostIndReg";
  let RenderMethod = "addPostIndRegOperands";
  let ParserMethod = "parsePostIndirectOperand";
}

// Branch target operand class
def TMS9900BrTargetAsmOperand : AsmOperandClass {
  let Name = "TMS9900BrTarget";
  let RenderMethod = "addBrTargetOperands";
  let ParserMethod = "parseBranchTarget";
}

//===----------------------------------------------------------------------===//
// Operand Definitions
//===----------------------------------------------------------------------===//

// 16-bit immediate
def imm16 : Operand<i16> {
  let ParserMatchClass = TMS9900ImmAsmOperand;
}

// 8-bit signed displacement for jumps (word offset, so range is -256 to +254 bytes)
def brtarget8 : Operand<OtherVT> {
  let EncoderMethod = "getBranchTargetEncoding";
  let DecoderMethod = "decodeBranchTarget";
  let ParserMatchClass = TMS9900BrTargetAsmOperand;
}

// 16-bit absolute branch target (for B @addr, used in branch relaxation)
def brtarget16 : Operand<OtherVT> {
  let EncoderMethod = "getBranchTarget16Encoding";
  let ParserMatchClass = TMS9900BrTargetAsmOperand;
}

// Memory operand (for symbolic addressing)
def memsrc : Operand<i16> {
  let MIOperandInfo = (ops GR16);
  let ParserMatchClass = TMS9900MemAsmOperand;
}

// Memory operand for indexed addressing: register + immediate offset
// Used for stack-relative addressing (SP + offset)
// MIOperandInfo defines this as a composite operand: (register, immediate)
def memri : Operand<i16> {
  let MIOperandInfo = (ops GR16, i16imm);
  let ParserMatchClass = TMS9900MemAsmOperand;
}

// Call target (for BL @symbol)
def calltarget : Operand<i16> {
  let EncoderMethod = "getCallTargetEncoding";
  let ParserMatchClass = TMS9900ImmAsmOperand;
}

//===----------------------------------------------------------------------===//
// Pattern Fragments
//===----------------------------------------------------------------------===//

// Load from memory
def load_i16 : PatFrag<(ops node:$ptr), (i16 (load node:$ptr))>;

// Store to memory
def store_i16 : PatFrag<(ops node:$val, node:$ptr), (store node:$val, node:$ptr)>;

//===----------------------------------------------------------------------===//
// Pseudo Instructions
//===----------------------------------------------------------------------===//

let isCodeGenOnly = 1, isPseudo = 1 in {

// Return instruction (lowered to B *R11)
def RET : Pseudo<(outs), (ins), "# RET", [(TMS9900ret)]> {
  let isReturn = 1;
  let isTerminator = 1;
  let isBarrier = 1;
}

} // isPseudo = 1, isCodeGenOnly = 1

// ADJCALLSTACKDOWN/UP implicitly use/def R10 (SP) because they may be expanded
// into a stack adjustment and the codegen must know they may modify SP
// before prolog-epilog rewriting occurs.
let isCodeGenOnly = 1, isPseudo = 1, Defs = [R10], Uses = [R10] in {

// ADJCALLSTACKDOWN - Pseudo for stack adjustment before call
def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i16imm:$amt, i16imm:$amt2),
                               "# ADJCALLSTACKDOWN $amt, $amt2",
                               [(TMS9900callseq_start timm:$amt, timm:$amt2)]>;

// ADJCALLSTACKUP - Pseudo for stack adjustment after call
def ADJCALLSTACKUP : Pseudo<(outs), (ins i16imm:$amt1, i16imm:$amt2),
                             "# ADJCALLSTACKUP $amt1, $amt2",
                             [(TMS9900callseq_end timm:$amt1, timm:$amt2)]>;

} // Defs = [R10], Uses = [R10]

// Other pseudos (general)
let isCodeGenOnly = 1, isPseudo = 1 in {

// AND pseudo - TMS9900 has no direct AND, so we use INV+SZC+INV sequence
// This computes rd = rs1 AND rs2
// Expanded to: INV rs2; SZC rs2, rd; INV rs2 (restores rs2)
// Note: rd must equal rs1 (two-address constraint)
def ANDrr : Pseudo<(outs GR16:$rd), (ins GR16:$rs1, GR16:$rs2),
                   "# AND $rs1, $rs2",
                   [(set GR16:$rd, (and GR16:$rs1, GR16:$rs2))]> {
  let Constraints = "$rd = $rs1";
  let usesCustomInserter = 0;
}

} // isPseudo = 1

//===----------------------------------------------------------------------===//
// Data Movement Instructions
// Note: All MOV variants set status bits (LGT, AGT, EQ) based on the value moved
//===----------------------------------------------------------------------===//

// MOV - Move word (register to register)
let Defs = [ST] in
def MOVrr : Format1_Reg<0b1100, 0, (outs GR16:$rd), (ins GR16:$rs),
                        "MOV\t$rs,$rd", []>;

//===----------------------------------------------------------------------===//
// Memory Load Instructions
//===----------------------------------------------------------------------===//

// MOV @addr,Rd - Load from absolute address
def MOVam : Format1_SymLoad<0b1100, 0, (outs GR16:$rd), (ins i16imm:$addr),
                            "MOV\t@$addr,$rd",
                            [(set GR16:$rd, (load (TMS9900Wrapper tglobaladdr:$addr)))]> {
  let mayLoad = 1;
}

// MOV *Rs,Rd - Load indirect (pointer dereference)
def MOVim : Format1_IndLoad<0b1100, 0, (outs GR16:$rd), (ins GR16:$rs),
                            "MOV\t*$rs,$rd",
                            [(set GR16:$rd, (load GR16:$rs))]> {
  let mayLoad = 1;
}

// MOV @offset(Ri),Rd - Load indexed
// Used for stack-relative addressing (locals via SP)
def MOVxm : Format1_IdxLoad<0b1100, 0, (outs GR16:$rd), (ins i16imm:$offset, GR16:$ri),
                            "MOV\t@${offset}(${ri}),$rd",
                            [(set GR16:$rd, (load (add GR16:$ri, (i16 imm:$offset))))]> {
  let mayLoad = 1;
}

//===----------------------------------------------------------------------===//
// Memory Store Instructions
//===----------------------------------------------------------------------===//

// MOV Rs,@addr - Store to absolute address
def MOVma : Format1_SymStore<0b1100, 0, (outs), (ins i16imm:$addr, GR16:$rs),
                             "MOV\t$rs,@$addr",
                             [(store GR16:$rs, (TMS9900Wrapper tglobaladdr:$addr))]> {
  let mayStore = 1;
}

// MOV Rs,*Rd - Store indirect
def MOVmi : Format1_IndStore<0b1100, 0, (outs), (ins GR16:$rd, GR16:$rs),
                             "MOV\t$rs,*$rd",
                             [(store GR16:$rs, GR16:$rd)]> {
  let mayStore = 1;
}

// MOV Rs,@offset(Ri) - Store indexed
// Used for stack-relative addressing (locals via SP)
def MOVmx : Format1_IdxStore<0b1100, 0, (outs), (ins i16imm:$offset, GR16:$ri, GR16:$rs),
                             "MOV\t$rs,@${offset}(${ri})",
                             [(store GR16:$rs, (add GR16:$ri, (i16 imm:$offset)))]> {
  let mayStore = 1;
}

//===----------------------------------------------------------------------===//
// Frame Index Compatible Load/Store Instructions
// These use the memri operand type which can hold a frame index + offset
// Used by storeRegToStackSlot and loadRegFromStackSlot
//===----------------------------------------------------------------------===//

// MOV @offset(Ri),Rd - Load indexed (frame index compatible)
// This is a pseudo that will be printed by custom code in TMS9900AsmPrinter
// or expanded by TMS9900MCInstLower
let mayLoad = 1, isCodeGenOnly = 1, isPseudo = 1 in
def MOV_FI_Load : TMS9900Inst<(outs GR16:$rd), (ins GR16:$base, i16imm:$offset),
                              "MOV\t@${offset}(${base}),$rd",
                              []> {
  let Size = 4;  // instruction word + offset word
}

// MOV Rs,@offset(Ri) - Store indexed (frame index compatible)
// This is a pseudo that will be printed by custom code in TMS9900AsmPrinter
// or expanded by TMS9900MCInstLower
let mayStore = 1, isCodeGenOnly = 1, isPseudo = 1 in
def MOV_FI_Store : TMS9900Inst<(outs), (ins GR16:$base, i16imm:$offset, GR16:$rs),
                               "MOV\t$rs,@${offset}(${base})",
                               []> {
  let Size = 4;  // instruction word + offset word
}

//===----------------------------------------------------------------------===//
// Byte Memory Load Instructions (MOVB)
// Note: Byte flag = 1, so opcode is 0b1101 for MOVB (vs 0b1100 for MOV)
// MOVB loads byte into HIGH 8 bits of register. We use TMS9900byteload
// which is created by LowerLOAD, then shifted right to move byte to LOW position.
//===----------------------------------------------------------------------===//

// MOVB *Rs,Rd - Load byte indirect
// Loads byte into upper 8 bits of register
def MOVBim : Format1_IndLoad<0b1100, 1, (outs GR16:$rd), (ins GR16:$rs),
                             "MOVB\t*$rs,$rd",
                             [(set GR16:$rd, (TMS9900byteload GR16:$rs))]> {
  let mayLoad = 1;
}

// MOVB @offset(Ri),Rd - Load byte indexed
def MOVBxm : Format1_IdxLoad<0b1100, 1, (outs GR16:$rd), (ins i16imm:$offset, GR16:$ri),
                             "MOVB\t@${offset}(${ri}),$rd",
                             [(set GR16:$rd, (TMS9900byteload (add GR16:$ri, (i16 imm:$offset))))]> {
  let mayLoad = 1;
}

// MOVB @addr,Rd - Load byte from absolute address
def MOVBam : Format1_SymLoad<0b1100, 1, (outs GR16:$rd), (ins i16imm:$addr),
                             "MOVB\t@$addr,$rd",
                             [(set GR16:$rd, (TMS9900byteload (TMS9900Wrapper tglobaladdr:$addr)))]> {
  let mayLoad = 1;
}

//===----------------------------------------------------------------------===//
// Byte Memory Store Instructions (MOVB)
//===----------------------------------------------------------------------===//

// MOVB Rs,*Rd - Store byte indirect
// Stores upper 8 bits of source register to memory
// Uses TMS9900bytestore which expects byte in HIGH position
def MOVBmi : Format1_IndStore<0b1100, 1, (outs), (ins GR16:$rd, GR16:$rs),
                              "MOVB\t$rs,*$rd",
                              [(TMS9900bytestore GR16:$rs, GR16:$rd)]> {
  let mayStore = 1;
}

// MOVB Rs,@offset(Ri) - Store byte indexed
def MOVBmx : Format1_IdxStore<0b1100, 1, (outs), (ins i16imm:$offset, GR16:$ri, GR16:$rs),
                              "MOVB\t$rs,@${offset}(${ri})",
                              [(TMS9900bytestore GR16:$rs, (add GR16:$ri, (i16 imm:$offset)))]> {
  let mayStore = 1;
}

// MOVB Rs,@addr - Store byte to absolute address
def MOVBma : Format1_SymStore<0b1100, 1, (outs), (ins i16imm:$addr, GR16:$rs),
                              "MOVB\t$rs,@$addr",
                              [(TMS9900bytestore GR16:$rs, (TMS9900Wrapper tglobaladdr:$addr))]> {
  let mayStore = 1;
}

//===----------------------------------------------------------------------===//
// Auto-Increment Load Instructions (MOV *Rs+,Rd)
// These load from memory and post-increment the address register
// Word operations increment by 2, byte operations increment by 1
//===----------------------------------------------------------------------===//

// MOV *Rs+,Rd - Load word with post-increment
// Output: $rd = loaded value, $rs_wb = $rs + 2
def MOVpim : Format1_AutoIncLoad<0b1100, 0, (outs GR16:$rd, GR16:$rs_wb),
                                 (ins GR16:$rs),
                                 "MOV\t*${rs}+,$rd", []> {
  let mayLoad = 1;
  let Constraints = "$rs_wb = $rs";
}

// MOVB *Rs+,Rd - Load byte with post-increment
// Output: $rd = loaded value (in upper byte), $rs_wb = $rs + 1
def MOVBpim : Format1_AutoIncLoad<0b1100, 1, (outs GR16:$rd, GR16:$rs_wb),
                                  (ins GR16:$rs),
                                  "MOVB\t*${rs}+,$rd", []> {
  let mayLoad = 1;
  let Constraints = "$rs_wb = $rs";
}

//===----------------------------------------------------------------------===//
// Auto-Increment Store Instructions (MOV Rs,*Rd+)
// These store to memory and post-increment the address register
//===----------------------------------------------------------------------===//

// MOV Rs,*Rd+ - Store word with post-increment
// Output: $rd_wb = $rd + 2
def MOVmpi : Format1_AutoIncStore<0b1100, 0, (outs GR16:$rd_wb),
                                  (ins GR16:$rd, GR16:$rs),
                                  "MOV\t$rs,*${rd}+", []> {
  let mayStore = 1;
  let Constraints = "$rd_wb = $rd";
}

// MOVB Rs,*Rd+ - Store byte with post-increment
// Output: $rd_wb = $rd + 1
def MOVBmpi : Format1_AutoIncStore<0b1100, 1, (outs GR16:$rd_wb),
                                   (ins GR16:$rd, GR16:$rs),
                                   "MOVB\t$rs,*${rd}+", []> {
  let mayStore = 1;
  let Constraints = "$rd_wb = $rd";
}

// LI - Load Immediate (sets status based on value loaded)
let Defs = [ST] in
def LI : Format8<0b0000, (outs GR16:$rd), (ins i16imm:$imm),
                 "LI\t$rd,$imm",
                 [(set GR16:$rd, imm:$imm)]>;

// CLR, SETO, SWPB all set status bits
let Defs = [ST] in {
// CLR - Clear (set to 0) - sets EQ=1
def CLRr : Format3_Reg<0b0000010011, (outs GR16:$rs), (ins),
                       "CLR\t$rs",
                       [(set GR16:$rs, 0)]>;

// SETO - Set to ones (FFFF) - sets LGT=1, AGT=0, EQ=0
def SETOr : Format3_Reg<0b0000011100, (outs GR16:$rs), (ins),
                        "SETO\t$rs",
                        [(set GR16:$rs, -1)]>;

// SWPB - Swap bytes - sets status based on result
def SWPBr : Format3_Reg<0b0000011011, (outs GR16:$rd), (ins GR16:$rs),
                        "SWPB\t$rs",
                        [(set GR16:$rd, (bswap GR16:$rs))]> {
  let Constraints = "$rd = $rs";
}
} // Defs = [ST]

//===----------------------------------------------------------------------===//
// Arithmetic Instructions
//===----------------------------------------------------------------------===//

// All arithmetic instructions set status bits (LGT, AGT, EQ, C, OV)
let Defs = [ST] in {

// A - Add
def Arr : Format1_Reg_TwoAddr<0b1010, 0, (outs GR16:$rd), (ins GR16:$rs1, GR16:$rs2),
                              "A\t$rs2,$rd",
                              [(set GR16:$rd, (add GR16:$rs1, GR16:$rs2))]> {
  let Constraints = "$rd = $rs1";
}

// AI - Add Immediate (subop=2)
def AI : Format8<0b0010, (outs GR16:$rd), (ins GR16:$rs, i16imm:$imm),
                 "AI\t$rs,$imm",
                 [(set GR16:$rd, (add GR16:$rs, imm:$imm))]> {
  let Constraints = "$rd = $rs";
}

// S - Subtract (dst = dst - src)
def Srr : Format1_Reg_TwoAddr<0b0110, 0, (outs GR16:$rd), (ins GR16:$rs1, GR16:$rs2),
                              "S\t$rs2,$rd",
                              [(set GR16:$rd, (sub GR16:$rs1, GR16:$rs2))]> {
  let Constraints = "$rd = $rs1";
}

// INC - Increment by 1
def INCr : Format3_Reg<0b0000010110, (outs GR16:$rd), (ins GR16:$rs),
                       "INC\t$rs",
                       [(set GR16:$rd, (add GR16:$rs, 1))]> {
  let Constraints = "$rd = $rs";
}

// INCT - Increment by 2
def INCTr : Format3_Reg<0b0000010111, (outs GR16:$rd), (ins GR16:$rs),
                        "INCT\t$rs",
                        [(set GR16:$rd, (add GR16:$rs, 2))]> {
  let Constraints = "$rd = $rs";
}

// DEC - Decrement by 1
def DECr : Format3_Reg<0b0000011000, (outs GR16:$rd), (ins GR16:$rs),
                       "DEC\t$rs",
                       [(set GR16:$rd, (sub GR16:$rs, 1))]> {
  let Constraints = "$rd = $rs";
}

// DECT - Decrement by 2
def DECTr : Format3_Reg<0b0000011001, (outs GR16:$rd), (ins GR16:$rs),
                        "DECT\t$rs",
                        [(set GR16:$rd, (sub GR16:$rs, 2))]> {
  let Constraints = "$rd = $rs";
}

// NEG - Negate (two's complement)
def NEGr : Format3_Reg<0b0000010100, (outs GR16:$rd), (ins GR16:$rs),
                       "NEG\t$rs",
                       [(set GR16:$rd, (ineg GR16:$rs))]> {
  let Constraints = "$rd = $rs";
}

// ABS - Absolute value
def ABSr : Format3_Reg<0b0000011101, (outs GR16:$rd), (ins GR16:$rs),
                       "ABS\t$rs",
                       [(set GR16:$rd, (abs GR16:$rs))]> {
  let Constraints = "$rd = $rs";
}

} // Defs = [ST]

//===----------------------------------------------------------------------===//
// Logical Instructions
// All logical instructions set status bits (LGT, AGT, EQ)
//===----------------------------------------------------------------------===//

let Defs = [ST] in {

// INV - Invert (one's complement)
def INVr : Format3_Reg<0b0000010101, (outs GR16:$rd), (ins GR16:$rs),
                       "INV\t$rs",
                       [(set GR16:$rd, (not GR16:$rs))]> {
  let Constraints = "$rd = $rs";
}

// SOC - Set Ones Corresponding (OR)
def SOCrr : Format1_Reg_TwoAddr<0b1110, 0, (outs GR16:$rd), (ins GR16:$rs1, GR16:$rs2),
                                "SOC\t$rs2,$rd",
                                [(set GR16:$rd, (or GR16:$rs1, GR16:$rs2))]> {
  let Constraints = "$rd = $rs1";
}

// ORI - OR Immediate (subop=6)
def ORI : Format8<0b0110, (outs GR16:$rd), (ins GR16:$rs, i16imm:$imm),
                  "ORI\t$rs,$imm",
                  [(set GR16:$rd, (or GR16:$rs, imm:$imm))]> {
  let Constraints = "$rd = $rs";
}

// SZC - Set Zeros Corresponding (AND with complement = BIC)
// dst = dst AND (NOT src)
def SZCrr : Format1_Reg_TwoAddr<0b0100, 0, (outs GR16:$rd), (ins GR16:$rs1, GR16:$rs2),
                                "SZC\t$rs2,$rd",
                                [(set GR16:$rd, (and GR16:$rs1, (not GR16:$rs2)))]> {
  let Constraints = "$rd = $rs1";
}

// ANDI - AND Immediate (subop=4)
def ANDI : Format8<0b0100, (outs GR16:$rd), (ins GR16:$rs, i16imm:$imm),
                   "ANDI\t$rs,$imm",
                   [(set GR16:$rd, (and GR16:$rs, imm:$imm))]> {
  let Constraints = "$rd = $rs";
}

// XOR - Exclusive OR
def XORrr : Format2_Reg_TwoAddr<0b001010, (outs GR16:$rd), (ins GR16:$rs1, GR16:$rs2),
                                "XOR\t$rs2,$rd",
                                [(set GR16:$rd, (xor GR16:$rs1, GR16:$rs2))]> {
  let Constraints = "$rd = $rs1";
}

} // Defs = [ST]

//===----------------------------------------------------------------------===//
// Shift Instructions
// All shift instructions set status bits (LGT, AGT, EQ, C)
//===----------------------------------------------------------------------===//

let Defs = [ST] in {

// SLA - Shift Left Arithmetic
def SLAri : Format7_TwoAddr<0b00001010, (outs GR16:$rd), (ins GR16:$rs, i16imm:$cnt),
                            "SLA\t$rs,$cnt",
                            [(set GR16:$rd, (shl GR16:$rs, (i16 imm:$cnt)))]> {
  let Constraints = "$rd = $rs";
}

// SRA - Shift Right Arithmetic
def SRAri : Format7_TwoAddr<0b00001000, (outs GR16:$rd), (ins GR16:$rs, i16imm:$cnt),
                            "SRA\t$rs,$cnt",
                            [(set GR16:$rd, (sra GR16:$rs, (i16 imm:$cnt)))]> {
  let Constraints = "$rd = $rs";
}

// SRL - Shift Right Logical
def SRLri : Format7_TwoAddr<0b00001001, (outs GR16:$rd), (ins GR16:$rs, i16imm:$cnt),
                            "SRL\t$rs,$cnt",
                            [(set GR16:$rd, (srl GR16:$rs, (i16 imm:$cnt)))]> {
  let Constraints = "$rd = $rs";
}

// SRC - Shift Right Circular (rotate right)
def SRCri : Format7_TwoAddr<0b00001011, (outs GR16:$rd), (ins GR16:$rs, i16imm:$cnt),
                            "SRC\t$rs,$cnt",
                            [(set GR16:$rd, (rotr GR16:$rs, (i16 imm:$cnt)))]> {
  let Constraints = "$rd = $rs";
}

} // Defs = [ST]

// Shift instructions with count from R0 (count field = 0)
// These are used by variable shift pseudos
let Defs = [ST] in {
def SLAr0 : Format7_R0Count<0b00001010, (outs GR16:$rd), (ins GR16:$rs),
                            "SLA\t$rs,0", []> {
  let Constraints = "$rd = $rs";
  // R0 is implicitly used for the shift count
  let Uses = [R0];
}

def SRAr0 : Format7_R0Count<0b00001000, (outs GR16:$rd), (ins GR16:$rs),
                            "SRA\t$rs,0", []> {
  let Constraints = "$rd = $rs";
  let Uses = [R0];
}

def SRLr0 : Format7_R0Count<0b00001001, (outs GR16:$rd), (ins GR16:$rs),
                            "SRL\t$rs,0", []> {
  let Constraints = "$rd = $rs";
  let Uses = [R0];
}
} // Defs = [ST]

// Variable shift instructions (shift count in register)
// These are pseudo instructions that expand to: MOV count, R0 + shift reg, 0
// The shift instructions use count=0 to mean "use R0 as shift count"

let usesCustomInserter = 1, Defs = [ST] in {
def SLA_VAR : Pseudo<(outs GR16:$rd), (ins GR16:$rs, GR16:$cnt),
                     "# SLA_VAR $rs, $cnt",
                     [(set GR16:$rd, (shl GR16:$rs, GR16:$cnt))]> {
  let Constraints = "$rd = $rs";
}

def SRA_VAR : Pseudo<(outs GR16:$rd), (ins GR16:$rs, GR16:$cnt),
                     "# SRA_VAR $rs, $cnt",
                     [(set GR16:$rd, (sra GR16:$rs, GR16:$cnt))]> {
  let Constraints = "$rd = $rs";
}

def SRL_VAR : Pseudo<(outs GR16:$rd), (ins GR16:$rs, GR16:$cnt),
                     "# SRL_VAR $rs, $cnt",
                     [(set GR16:$rd, (srl GR16:$rs, GR16:$cnt))]> {
  let Constraints = "$rd = $rs";
}
}

//===----------------------------------------------------------------------===//
// Compare Instructions
//===----------------------------------------------------------------------===//

// C - Compare
let Defs = [ST] in {
def Crr : Format1_Reg_Cmp<0b1000, 0, (outs), (ins GR16:$rs1, GR16:$rs2),
                          "C\t$rs1,$rs2",
                          [(TMS9900cmp GR16:$rs1, GR16:$rs2)]>;

// CI - Compare Immediate (subop=8)
def CI : Format8_Cmp<0b1000, (outs), (ins GR16:$rs, i16imm:$imm),
                     "CI\t$rs,$imm",
                 [(TMS9900cmp GR16:$rs, imm:$imm)]>;
}

//===----------------------------------------------------------------------===//
// Branch and Jump Instructions
//===----------------------------------------------------------------------===//

// B - Unconditional Branch (single operand format, but we use register)
def Br : Format3_Reg<0b0000010001, (outs), (ins GR16:$rs),
                     "B\t*$rs",
                     [(brind GR16:$rs)]> {
  let isBranch = 1;
  let isTerminator = 1;
  let isBarrier = 1;
  let isIndirectBranch = 1;
}

// B with symbolic address - B @symbol (for branch relaxation)
// Format 3 with symbolic addressing: 4 bytes (opcode + address)
let isBranch = 1, isTerminator = 1, isBarrier = 1 in
def B_sym : TMS9900Inst<(outs), (ins brtarget16:$target),
                        "B\t@$target",
                        []> {
  bits<16> target;
  bits<32> Inst;
  // First word: opcode with symbolic addressing mode
  // B opcode is 0000010001 (10 bits), then Ts=10, S=0000
  let Inst{15-6} = 0b0000010001;  // B opcode (10 bits)
  let Inst{5-4} = 0b10;           // Ts = symbolic addressing
  let Inst{3-0} = 0b0000;         // S = 0 (address follows)
  // Second word: target address
  let Inst{31-16} = target;
  let Size = 4;
}

// JMP - Unconditional jump (PC-relative)
def JMP : Format6<0b00010000, (outs), (ins brtarget8:$disp),
                  "JMP\t$disp",
                  [(br bb:$disp)]>;

// Conditional jumps - all are branches that use the status register
let Uses = [ST], isBranch = 1, isTerminator = 1 in {
def JEQ : Format6<0b00010011, (outs), (ins brtarget8:$disp),
                  "JEQ\t$disp", []>;
def JNE : Format6<0b00010110, (outs), (ins brtarget8:$disp),
                  "JNE\t$disp", []>;
def JGT : Format6<0b00010101, (outs), (ins brtarget8:$disp),
                  "JGT\t$disp", []>;
def JLT : Format6<0b00010001, (outs), (ins brtarget8:$disp),
                  "JLT\t$disp", []>;
def JH  : Format6<0b00011011, (outs), (ins brtarget8:$disp),
                  "JH\t$disp", []>;
def JHE : Format6<0b00010100, (outs), (ins brtarget8:$disp),
                  "JHE\t$disp", []>;
def JL  : Format6<0b00011010, (outs), (ins brtarget8:$disp),
                  "JL\t$disp", []>;
def JLE : Format6<0b00010010, (outs), (ins brtarget8:$disp),
                  "JLE\t$disp", []>;
def JOC : Format6<0b00011000, (outs), (ins brtarget8:$disp),
                  "JOC\t$disp", []>;
def JNC : Format6<0b00010111, (outs), (ins brtarget8:$disp),
                  "JNC\t$disp", []>;
def JNO : Format6<0b00011001, (outs), (ins brtarget8:$disp),
                  "JNO\t$disp", []>;
def JOP : Format6<0b00011100, (outs), (ins brtarget8:$disp),
                  "JOP\t$disp", []>;
}

//===----------------------------------------------------------------------===//
// Call and Return Instructions
//===----------------------------------------------------------------------===//

// BL - Branch and Link with register indirect (stores return address in R11)
def BL : Format3_Reg<0b0000011010, (outs), (ins GR16:$rs),
                     "BL\t*$rs",
                     [(TMS9900call GR16:$rs)]> {
  let isCall = 1;
  let Defs = [R11];
}

// BL with symbolic address - BL @symbol
// Format 3 with symbolic addressing: 4 bytes (opcode + address)
let isCall = 1, Defs = [R11] in
def BL_sym : TMS9900Inst<(outs), (ins calltarget:$target),
                         "BL\t@$target",
                         [(TMS9900call tglobaladdr:$target)]> {
  bits<16> target;
  bits<32> Inst;
  // First word: opcode with symbolic addressing mode
  // BL opcode is 0000011010 (10 bits), then Ts=10, S=0000
  let Inst{15-6} = 0b0000011010;  // BL opcode (10 bits)
  let Inst{5-4} = 0b10;           // Ts = symbolic addressing
  let Inst{3-0} = 0b0000;         // S = 0 (address follows)
  // Second word: target address
  let Inst{31-16} = target;
  let Size = 4;
}

// BL with external symbol - BL @external
// Marked isCodeGenOnly to avoid duplicate asm pattern with BL_sym
let isCall = 1, Defs = [R11] in
def BL_ext : TMS9900Inst<(outs), (ins calltarget:$target),
                         "BL\t@$target",
                         [(TMS9900call texternalsym:$target)]> {
  bits<16> target;
  bits<32> Inst;
  // First word: opcode with symbolic addressing mode
  // BL opcode is 0000011010 (10 bits), then Ts=10, S=0000
  let Inst{15-6} = 0b0000011010;  // BL opcode (10 bits)
  let Inst{5-4} = 0b10;           // Ts = symbolic addressing
  let Inst{3-0} = 0b0000;         // S = 0 (address follows)
  // Second word: target address
  let Inst{31-16} = target;
  let Size = 4;
  let isCodeGenOnly = 1;
}

// RTWP - Return with workspace pointer (full context restore)
// Used for returning from interrupt handlers
def RTWP : Format11<0b0000001110000000, (outs), (ins),
                    "RTWP",
                    [(TMS9900reti)]> {
  let isReturn = 1;
  let isTerminator = 1;
  let isBarrier = 1;
}

// Simple return via B *R11 (for non-context-switch returns)
// This is lowered from the RET pseudo
def RET_REAL : Format3_Reg_R11<0b0000010001, (outs), (ins),
                               "B\t*R11",
                               []> {
  let isReturn = 1;
  let isTerminator = 1;
  let isBarrier = 1;
  let Uses = [R11];
}

//===----------------------------------------------------------------------===//
// Multiply and Divide
//===----------------------------------------------------------------------===//

// MPY - Multiply (unsigned 16x16 -> 32)
// The instruction format is: MPY Rs,Rd
// Before: Rd contains multiplicand1
// After: Rd:Rd+1 = multiplicand1 * Rs (32-bit result)
//        Rd has high 16 bits, Rd+1 has low 16 bits
// Note: Rd must be an even register (R0, R2, R4, R6, R8)
// We hardcode to R0 and mark that it uses R0 and clobbers R0:R1
let Uses = [R0], Defs = [R0, R1] in
def MPYrr : Format2_Reg_R0<0b001110, (outs), (ins GR16:$rs),
                           "MPY\t$rs,R0",
                           []>;

// DIV - Divide (unsigned 32/16 -> 16q, 16r)
// Dividend in Rd:Rd+1 (32-bit input), divisor in Rs
// Result: quotient in Rd, remainder in Rd+1
// Note: Rd must be an even register
// The instruction reads and writes R0:R1 (when Rd=R0)
let Uses = [R0, R1], Defs = [R0, R1] in
def DIVrr : Format2_Reg_R0<0b001111, (outs), (ins GR16:$rs),
                           "DIV\t$rs,R0",
                           []>;

// Pseudo instruction for 16-bit multiply (result = low 16 bits)
// This is expanded by the custom inserter to:
// 1. MOV $lhs, R0 (or other even register)
// 2. MPY $rhs, R0
// 3. MOV R1, $dst (get low 16 bits from R1)
let usesCustomInserter = 1, Defs = [R0, R1] in
def MUL16 : Pseudo<(outs GR16:$dst), (ins GR16:$lhs, GR16:$rhs),
                   "# MUL16 $lhs, $rhs -> $dst",
                   [(set GR16:$dst, (mul GR16:$lhs, GR16:$rhs))]>;

// Pseudo instruction for unsigned divide (result = quotient)
// Expands to:
// 1. CLR R0 (high word of dividend = 0 for 16-bit dividend)
// 2. MOV $dividend, R1 (low word of dividend)
// 3. DIV $divisor, R0
// 4. MOV R0, $quotient (quotient is in R0)
let usesCustomInserter = 1, Defs = [R0, R1] in
def UDIV16 : Pseudo<(outs GR16:$dst), (ins GR16:$lhs, GR16:$rhs),
                    "# UDIV16 $lhs / $rhs -> $dst",
                    [(set GR16:$dst, (udiv GR16:$lhs, GR16:$rhs))]>;

// Pseudo instruction for unsigned remainder
// Same setup as UDIV16, but result comes from R1
let usesCustomInserter = 1, Defs = [R0, R1] in
def UREM16 : Pseudo<(outs GR16:$dst), (ins GR16:$lhs, GR16:$rhs),
                    "# UREM16 $lhs %% $rhs -> $dst",
                    [(set GR16:$dst, (urem GR16:$lhs, GR16:$rhs))]>;

// Pseudo instruction for signed divide
// TMS9900 DIV is unsigned, so we handle signs:
// 1. Compute sign of result (XOR of input signs)
// 2. Take absolute values
// 3. Unsigned divide
// 4. Negate result if signs differed
let usesCustomInserter = 1, Defs = [R0, R1, R2, R3] in
def SDIV16 : Pseudo<(outs GR16:$dst), (ins GR16:$lhs, GR16:$rhs),
                    "# SDIV16 $lhs / $rhs -> $dst",
                    [(set GR16:$dst, (sdiv GR16:$lhs, GR16:$rhs))]>;

// Pseudo instruction for signed remainder
// Remainder has the sign of the dividend (C99 semantics)
let usesCustomInserter = 1, Defs = [R0, R1, R2, R3] in
def SREM16 : Pseudo<(outs GR16:$dst), (ins GR16:$lhs, GR16:$rhs),
                    "# SREM16 $lhs %% $rhs -> $dst",
                    [(set GR16:$dst, (srem GR16:$lhs, GR16:$rhs))]>;

// Pseudo instruction for SELECT_CC
// Implements conditional select: dst = (lhs CC rhs) ? trueval : falseval
// Expanded by custom inserter into compare + conditional branches
let usesCustomInserter = 1, Defs = [ST] in
def SELECT16 : Pseudo<(outs GR16:$dst),
                      (ins GR16:$trueval, GR16:$falseval, i16imm:$cc, GR16:$lhs, GR16:$rhs),
                      "# SELECT16 $trueval, $falseval, $cc, $lhs, $rhs -> $dst",
                      [(set GR16:$dst,
                        (TMS9900selectcc GR16:$trueval, GR16:$falseval, imm:$cc,
                                          GR16:$lhs, GR16:$rhs))]>;

//===----------------------------------------------------------------------===//
// Internal Register Instructions
//===----------------------------------------------------------------------===//

// LWPI - Load Workspace Pointer Immediate
def LWPI : Format9<0b00000010111, (outs), (ins i16imm:$imm),
                   "LWPI\t$imm",
                   []> {
  let Defs = [WP];
}

// LIMI - Load Interrupt Mask Immediate
def LIMI : Format9<0b00000011000, (outs), (ins i16imm:$imm),
                   "LIMI\t$imm",
                   []>;

// STST - Store Status Register
def STST : TMS9900Inst<(outs GR16:$rd), (ins),
                       "STST\t$rd",
                       []> {
  bits<4> rd;
  bits<16> Inst;
  let Inst{15-5} = 0b00000010110;
  let Inst{4} = 0;
  let Inst{3-0} = rd;
  let Uses = [ST];
}

// STWP - Store Workspace Pointer
def STWP : TMS9900Inst<(outs GR16:$rd), (ins),
                       "STWP\t$rd",
                       []> {
  bits<4> rd;
  bits<16> Inst;
  let Inst{15-5} = 0b00000010101;
  let Inst{4} = 0;
  let Inst{3-0} = rd;
  let Uses = [WP];
}

//===----------------------------------------------------------------------===//
// External Instructions
//===----------------------------------------------------------------------===//

def IDLE : Format11<0b0000001101000000, (outs), (ins),
                    "IDLE",
                    []>;

def RSET : Format11<0b0000001101100000, (outs), (ins),
                    "RSET",
                    []>;

//===----------------------------------------------------------------------===//
// NOP (pseudo - could be various things, using JMP $+2)
//===----------------------------------------------------------------------===//

def NOP : Pseudo<(outs), (ins), "NOP", []>;

//===----------------------------------------------------------------------===//
// Additional Patterns for Global/Jump Table/Block Addresses
//===----------------------------------------------------------------------===//

// Load address of global symbol into register: LI Rd,@symbol
def : Pat<(TMS9900Wrapper tglobaladdr:$addr),
          (LI tglobaladdr:$addr)>;

// Load address of jump table into register: LI Rd,@JTI_x_y
def : Pat<(TMS9900Wrapper tjumptable:$addr),
          (LI tjumptable:$addr)>;

// Load address of block (for computed goto) into register: LI Rd,@BB
def : Pat<(TMS9900Wrapper tblockaddress:$addr),
          (LI tblockaddress:$addr)>;

// Load from jump table with index: MOV @TABLE(Rx),Rd
// This matches loading from (wrapper + reg), used for jump table indexing
def : Pat<(load (add (TMS9900Wrapper tjumptable:$table), GR16:$idx)),
          (MOVxm tjumptable:$table, GR16:$idx)>;
