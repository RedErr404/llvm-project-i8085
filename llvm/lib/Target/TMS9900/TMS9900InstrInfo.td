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

def TMS9900tailcall : SDNode<"TMS9900ISD::TAIL_CALL", SDT_TMS9900Call,
                             [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                              SDNPVariadic]>;

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
                                                  SDTCisSameAs<4, 5>]> >;

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

def TMS9900Imm4AsmOperand : AsmOperandClass {
  let Name = "TMS9900Imm4";
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isTMS9900Imm4";
}

def TMS9900CRUCountAsmOperand : AsmOperandClass {
  let Name = "TMS9900CRUCount";
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isTMS9900CRUCount";
}

def TMS9900CRUDispAsmOperand : AsmOperandClass {
  let Name = "TMS9900CRUDisp";
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isTMS9900CRUDisp";
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

// 4-bit immediate
def imm4 : Operand<i16> {
  let ParserMatchClass = TMS9900Imm4AsmOperand;
}

// Shift count (0 uses R0 for the count)
def shcount : Operand<i16> {
  let ParserMatchClass = TMS9900Imm4AsmOperand;
  let DecoderMethod = "decodeShiftCount";
}

// CRU count (0-15, 0 means 16 bits)
def cru_count : Operand<i16> {
  let ParserMatchClass = TMS9900CRUCountAsmOperand;
}

// CRU signed displacement (-128..127)
def cru_disp8 : Operand<i16> {
  let ParserMatchClass = TMS9900CRUDispAsmOperand;
  let EncoderMethod = "getCRUDispEncoding";
  let DecoderMethod = "decodeCRUDisp";
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
  let PrintMethod = "printBranchTarget16";
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
  let PrintMethod = "printBranchTarget16";
  let ParserMatchClass = TMS9900BrTargetAsmOperand;
}

// Symbolic address for memory operands (MOV @addr, etc.)
def symaddr : Operand<i16> {
  let PrintMethod = "printBranchTarget16";
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

// Tail call pseudo instructions
// These are used for tail call optimization where we branch directly to the
// callee instead of using BL (which saves the return address).
// The callee will return directly to our caller via the existing R11.
let isCodeGenOnly = 1, isPseudo = 1 in {

// TCRETURN with symbolic address - tail call to a global function
let isCall = 1, isReturn = 1, isTerminator = 1, isBarrier = 1,
    Uses = [R10] in
def TCRETURN : Pseudo<(outs), (ins calltarget:$dst),
                       "# TCRETURN $dst",
                       [(TMS9900tailcall tglobaladdr:$dst)]>;

// TCRETURN with external symbol
let isCall = 1, isReturn = 1, isTerminator = 1, isBarrier = 1,
    Uses = [R10] in
def TCRETURN_ext : Pseudo<(outs), (ins calltarget:$dst),
                           "# TCRETURN $dst",
                           [(TMS9900tailcall texternalsym:$dst)]>;

// TCRETURN with indirect register - tail call to a function pointer
let isCall = 1, isReturn = 1, isTerminator = 1, isBarrier = 1,
    Uses = [R10] in
def TCRETURN_ind : Pseudo<(outs), (ins GR16:$dst),
                           "# TCRETURN *$dst",
                           [(TMS9900tailcall GR16:$dst)]>;

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
// Defs = [ST] is required because the expansion (INV, SZC, INV) all set
// status flags. Without it, the scheduler may move ANDrr across
// flag-dependent sequences, corrupting comparison results.
let Defs = [ST] in
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

// MOVB - Move byte (register to register)
let Defs = [ST] in
def MOVBrr : Format1_Reg<0b1100, 1, (outs GR16:$rd), (ins GR16:$rs),
                         "MOVB\t$rs,$rd", []>;

//===----------------------------------------------------------------------===//
// Memory Load Instructions
//===----------------------------------------------------------------------===//

let Defs = [ST] in {
// MOV @addr,Rd - Load from absolute address
// Marked as rematerializable; verified by isReallyTriviallyReMaterializable
// which checks that the address refers to a constant global or constant pool.
let isReMaterializable = 1 in
def MOVam : Format1_SymLoad<0b1100, 0, (outs GR16:$rd), (ins symaddr:$addr),
                            "MOV\t@$addr,$rd",
                            [(set GR16:$rd, (load (TMS9900Wrapper tglobaladdr:$addr)))]> {
  let mayLoad = 1;
}

// MOV *Rs,Rd - Load indirect (pointer dereference)
// NOT rematerializable: the pointer register value could change between
// the original load and the rematerialization point.
def MOVim : Format1_IndLoad<0b1100, 0, (outs GR16:$rd), (ins GR16:$rs),
                            "MOV\t*$rs,$rd",
                            [(set GR16:$rd, (load GR16:$rs))]> {
  let mayLoad = 1;
}

// MOV @offset(Ri),Rd - Load indexed
// Used for stack-relative addressing (locals via SP)
// Marked as rematerializable; verified by isReallyTriviallyReMaterializable
// which checks that the base register is the frame pointer (R10).
let isReMaterializable = 1 in
def MOVxm : Format1_IdxLoad<0b1100, 0, (outs GR16:$rd), (ins i16imm:$offset, IdxRegs:$ri),
                            "MOV\t@${offset}(${ri}),$rd",
                            [(set GR16:$rd, (load (add IdxRegs:$ri, (i16 imm:$offset))))]> {
  let mayLoad = 1;
}
} // Defs = [ST]

//===----------------------------------------------------------------------===//
// Memory Store Instructions
//===----------------------------------------------------------------------===//

let Defs = [ST] in {
// MOV Rs,@addr - Store to absolute address
def MOVma : Format1_SymStore<0b1100, 0, (outs), (ins symaddr:$addr, GR16:$rs),
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
def MOVmx : Format1_IdxStore<0b1100, 0, (outs), (ins i16imm:$offset, IdxRegs:$ri, GR16:$rs),
                             "MOV\t$rs,@${offset}(${ri})",
                             [(store GR16:$rs, (add IdxRegs:$ri, (i16 imm:$offset)))]> {
  let mayStore = 1;
}
} // Defs = [ST]

//===----------------------------------------------------------------------===//
// Frame Index Compatible Load/Store Instructions
// These use the memri operand type which can hold a frame index + offset
// Used by storeRegToStackSlot and loadRegFromStackSlot
//===----------------------------------------------------------------------===//

// MOV @offset(Ri),Rd - Load indexed (frame index compatible)
// This is a pseudo that will be printed by custom code in TMS9900AsmPrinter
// or expanded by TMS9900MCInstLower
// Marked as rematerializable; verified by isReallyTriviallyReMaterializable
// which checks that the base is a frame index pointing to an immutable object.
let Defs = [ST], mayLoad = 1, isCodeGenOnly = 1, isPseudo = 1,
    isReMaterializable = 1 in
def MOV_FI_Load : TMS9900Inst<(outs GR16:$rd), (ins IdxRegs:$base, i16imm:$offset),
                              "MOV\t@${offset}(${base}),$rd",
                              []> {
  let Size = 4;  // instruction word + offset word
}

// MOV Rs,@offset(Ri) - Store indexed (frame index compatible)
// This is a pseudo that will be printed by custom code in TMS9900AsmPrinter
// or expanded by TMS9900MCInstLower
let Defs = [ST], mayStore = 1, isCodeGenOnly = 1, isPseudo = 1 in
def MOV_FI_Store : TMS9900Inst<(outs), (ins IdxRegs:$base, i16imm:$offset, GR16:$rs),
                               "MOV\t$rs,@${offset}(${base})",
                               []> {
  let Size = 4;  // instruction word + offset word
}

// LEAfi - Load Effective Address of frame index
// Computes the address of a stack slot into a register.
// This is expanded during frame index elimination to:
//   MOV R10, $rd       ; Copy stack pointer to dest
//   AI  $rd, offset    ; Add frame offset
// Used when a pointer to a local variable needs to be passed to a function.
// Uses memri operand which can hold frame index + offset.
let Defs = [ST], isCodeGenOnly = 1, isPseudo = 1, Uses = [R10] in
def LEAfi : Pseudo<(outs GR16:$rd), (ins memri:$addr),
                   "# LEA $rd, $addr",
                   []> {
  let Size = 6;  // MOV (2) + AI (4)
}

//===----------------------------------------------------------------------===//
// Byte Memory Load Instructions (MOVB)
// Note: Byte flag = 1, so opcode is 0b1101 for MOVB (vs 0b1100 for MOV)
// MOVB loads byte into HIGH 8 bits of register. We use TMS9900byteload
// which is created by LowerLOAD, then shifted right to move byte to LOW position.
//===----------------------------------------------------------------------===//

let Defs = [ST] in {
// MOVB *Rs,Rd - Load byte indirect
// Loads byte into upper 8 bits of register
def MOVBim : Format1_IndLoad<0b1100, 1, (outs GR16:$rd), (ins GR16:$rs),
                             "MOVB\t*$rs,$rd",
                             [(set GR16:$rd, (TMS9900byteload GR16:$rs))]> {
  let mayLoad = 1;
}

// MOVB @offset(Ri),Rd - Load byte indexed
def MOVBxm : Format1_IdxLoad<0b1100, 1, (outs GR16:$rd), (ins i16imm:$offset, IdxRegs:$ri),
                             "MOVB\t@${offset}(${ri}),$rd",
                             [(set GR16:$rd, (TMS9900byteload (add IdxRegs:$ri, (i16 imm:$offset))))]> {
  let mayLoad = 1;
}

// MOVB @addr,Rd - Load byte from absolute address
def MOVBam : Format1_SymLoad<0b1100, 1, (outs GR16:$rd), (ins symaddr:$addr),
                             "MOVB\t@$addr,$rd",
                             [(set GR16:$rd, (TMS9900byteload (TMS9900Wrapper tglobaladdr:$addr)))]> {
  let mayLoad = 1;
}
} // Defs = [ST]

//===----------------------------------------------------------------------===//
// Byte Memory Store Instructions (MOVB)
//===----------------------------------------------------------------------===//

let Defs = [ST] in {
// MOVB Rs,*Rd - Store byte indirect
// Stores upper 8 bits of source register to memory
// Uses TMS9900bytestore which expects byte in HIGH position
def MOVBmi : Format1_IndStore<0b1100, 1, (outs), (ins GR16:$rd, GR16:$rs),
                              "MOVB\t$rs,*$rd",
                              [(TMS9900bytestore GR16:$rs, GR16:$rd)]> {
  let mayStore = 1;
}

// MOVB Rs,@offset(Ri) - Store byte indexed
def MOVBmx : Format1_IdxStore<0b1100, 1, (outs), (ins i16imm:$offset, IdxRegs:$ri, GR16:$rs),
                              "MOVB\t$rs,@${offset}(${ri})",
                              [(TMS9900bytestore GR16:$rs, (add IdxRegs:$ri, (i16 imm:$offset)))]> {
  let mayStore = 1;
}

// MOVB Rs,@addr - Store byte to absolute address
def MOVBma : Format1_SymStore<0b1100, 1, (outs), (ins symaddr:$addr, GR16:$rs),
                              "MOVB\t$rs,@$addr",
                              [(TMS9900bytestore GR16:$rs, (TMS9900Wrapper tglobaladdr:$addr))]> {
  let mayStore = 1;
}
} // Defs = [ST]

//===----------------------------------------------------------------------===//
// Auto-Increment Load Instructions (MOV *Rs+,Rd)
// These load from memory and post-increment the address register
// Word operations increment by 2, byte operations increment by 1
//===----------------------------------------------------------------------===//

let Defs = [ST] in {
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
} // Defs = [ST]

//===----------------------------------------------------------------------===//
// Auto-Increment Store Instructions (MOV Rs,*Rd+)
// These store to memory and post-increment the address register
//===----------------------------------------------------------------------===//

let Defs = [ST] in {
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
} // Defs = [ST]

//===----------------------------------------------------------------------===//
// Format 1 Mem-to-Mem (Symbolic/Indexed on Both Operands)
//===----------------------------------------------------------------------===//

multiclass Format1MemMem<bits<4> opcode, bit byteflag, string asmname> {
  def aa : Format1_SymSym<opcode, byteflag, (outs),
                          (ins symaddr:$src, symaddr:$dst),
                          !strconcat(asmname, "\t@$src,@$dst"), []>;
  def ax : Format1_SymIdx<opcode, byteflag, (outs),
                          (ins symaddr:$src, i16imm:$dst, IdxRegs:$ri),
                          !strconcat(asmname, "\t@$src,@${dst}(${ri})"), []>;
  def xa : Format1_IdxSym<opcode, byteflag, (outs),
                          (ins i16imm:$src, IdxRegs:$ri, symaddr:$dst),
                          !strconcat(asmname, "\t@${src}(${ri}),@$dst"), []>;
  def xx : Format1_IdxIdx<opcode, byteflag, (outs),
                          (ins i16imm:$src, IdxRegs:$src_ri,
                               i16imm:$dst, IdxRegs:$dst_ri),
                          !strconcat(asmname,
                                     "\t@${src}(${src_ri}),@${dst}(${dst_ri})"),
                          []>;
}

let Defs = [ST], mayLoad = 1, mayStore = 1 in {
defm MOV  : Format1MemMem<0b1100, 0, "MOV">;
defm MOVB : Format1MemMem<0b1100, 1, "MOVB">;
defm A    : Format1MemMem<0b1010, 0, "A">;
defm AB   : Format1MemMem<0b1010, 1, "AB">;
defm S    : Format1MemMem<0b0110, 0, "S">;
defm SB   : Format1MemMem<0b0110, 1, "SB">;
defm SOC  : Format1MemMem<0b1110, 0, "SOC">;
defm SOCB : Format1MemMem<0b1110, 1, "SOCB">;
defm SZC  : Format1MemMem<0b0100, 0, "SZC">;
defm SZCB : Format1MemMem<0b0100, 1, "SZCB">;
}

let Defs = [ST], mayLoad = 1 in {
defm C  : Format1MemMem<0b1000, 0, "C">;
defm CB : Format1MemMem<0b1000, 1, "CB">;
}

//===----------------------------------------------------------------------===//
// Format 1 Extended Mem-to-Mem (Remaining Addressing Mode Combinations)
// Covers indirect (*Rn), auto-increment (*Rn+) with non-register partners.
// These are assembly-only (no DAG patterns) since codegen uses register
// intermediaries for these combinations.
//===----------------------------------------------------------------------===//

multiclass Format1ExtMem<bits<4> opcode, bit byteflag, string asmname> {
  // Indirect ↔ Indirect
  def ii : Format1_IndInd<opcode, byteflag, (outs),
                          (ins GR16:$rd, GR16:$rs),
                          !strconcat(asmname, "\t*$rs,*$rd"), []>;
  // Symbolic → Indirect
  def ai : Format1_SymInd<opcode, byteflag, (outs),
                          (ins GR16:$rd, symaddr:$addr),
                          !strconcat(asmname, "\t@$addr,*$rd"), []>;
  // Indexed → Indirect
  def xi : Format1_IdxInd<opcode, byteflag, (outs),
                          (ins GR16:$rd, i16imm:$offset, IdxRegs:$ri),
                          !strconcat(asmname, "\t@${offset}(${ri}),*$rd"), []>;
  // AutoInc → Indirect
  def pii : Format1_AutoIncInd<opcode, byteflag, (outs),
                               (ins GR16:$rd, GR16:$rs),
                               !strconcat(asmname, "\t*${rs}+,*$rd"), []>;
  // Indirect → Symbolic
  def ia : Format1_IndSym<opcode, byteflag, (outs),
                          (ins symaddr:$addr, GR16:$rs),
                          !strconcat(asmname, "\t*$rs,@$addr"), []>;
  // Indirect → Indexed
  def ix : Format1_IndIdx<opcode, byteflag, (outs),
                          (ins i16imm:$offset, IdxRegs:$ri, GR16:$rs),
                          !strconcat(asmname, "\t*$rs,@${offset}(${ri})"), []>;
  // Indirect → AutoInc
  def ipi : Format1_IndAutoInc<opcode, byteflag, (outs),
                               (ins GR16:$rd, GR16:$rs),
                               !strconcat(asmname, "\t*$rs,*${rd}+"), []>;
  // Symbolic → AutoInc
  def api : Format1_SymAutoInc<opcode, byteflag, (outs),
                               (ins GR16:$rd, symaddr:$addr),
                               !strconcat(asmname, "\t@$addr,*${rd}+"), []>;
  // Indexed → AutoInc
  def xpi : Format1_IdxAutoInc<opcode, byteflag, (outs),
                               (ins GR16:$rd, i16imm:$offset, IdxRegs:$ri),
                               !strconcat(asmname,
                                          "\t@${offset}(${ri}),*${rd}+"), []>;
  // AutoInc → Symbolic
  def pia : Format1_AutoIncSym<opcode, byteflag, (outs),
                               (ins symaddr:$addr, GR16:$rs),
                               !strconcat(asmname, "\t*${rs}+,@$addr"), []>;
  // AutoInc → Indexed
  def pix : Format1_AutoIncIdx<opcode, byteflag, (outs),
                               (ins i16imm:$offset, IdxRegs:$ri, GR16:$rs),
                               !strconcat(asmname,
                                          "\t*${rs}+,@${offset}(${ri})"), []>;
  // AutoInc → AutoInc
  def pipi : Format1_AutoIncAutoInc<opcode, byteflag, (outs),
                                    (ins GR16:$rd, GR16:$rs),
                                    !strconcat(asmname, "\t*${rs}+,*${rd}+"),
                                    []>;
}

let Defs = [ST], mayLoad = 1, mayStore = 1 in {
defm MOV  : Format1ExtMem<0b1100, 0, "MOV">;
defm MOVB : Format1ExtMem<0b1100, 1, "MOVB">;
defm A    : Format1ExtMem<0b1010, 0, "A">;
defm AB   : Format1ExtMem<0b1010, 1, "AB">;
defm S    : Format1ExtMem<0b0110, 0, "S">;
defm SB   : Format1ExtMem<0b0110, 1, "SB">;
defm SOC  : Format1ExtMem<0b1110, 0, "SOC">;
defm SOCB : Format1ExtMem<0b1110, 1, "SOCB">;
defm SZC  : Format1ExtMem<0b0100, 0, "SZC">;
defm SZCB : Format1ExtMem<0b0100, 1, "SZCB">;
}

let Defs = [ST], mayLoad = 1 in {
defm C  : Format1ExtMem<0b1000, 0, "C">;
defm CB : Format1ExtMem<0b1000, 1, "CB">;
}

// LI - Load Immediate (sets status based on value loaded)
// Always safe to rematerialize since the immediate is encoded in the instruction.
let Defs = [ST], isReMaterializable = 1, isAsCheapAsAMove = 1 in
def LI : Format8<0b0000, (outs GR16:$rd), (ins i16imm:$imm),
                 "LI\t$rd,$imm",
                 [(set GR16:$rd, imm:$imm)]> {
  let SchedRW = [WriteImm12];
}

// CLR and SETO do NOT affect the status register (TMS9900 Data Manual p.24:
// Status Bits Affected = "--" for both CLR and SETO).
// SWPB also does NOT affect status.
let SchedRW = [WriteFast] in {
// CLR - Clear (set to 0) - does NOT set status flags
// Always safe to rematerialize (produces constant 0).
let isReMaterializable = 1, isAsCheapAsAMove = 1 in
def CLRr : Format3_Reg<0b0000010011, (outs GR16:$rs), (ins),
                       "CLR\t$rs",
                       [(set GR16:$rs, 0)]>;

def CLR_ind : Format3_IndReg<0b0000010011, (outs), (ins GR16:$rs),
                             "CLR\t*$rs",
                             []> {
  let mayStore = 1;
}

def CLR_sym : Format3_Sym<0b0000010011, (outs), (ins symaddr:$addr),
                          "CLR\t@$addr",
                          []> {
  let mayStore = 1;
}

def CLR_idx : Format3_Idx<0b0000010011, (outs),
                          (ins i16imm:$offset, IdxRegs:$ri),
                          "CLR\t@${offset}(${ri})",
                          []> {
  let mayStore = 1;
}

def CLR_pim : Format3_AutoInc<0b0000010011, (outs), (ins GR16:$rs),
                              "CLR\t*${rs}+",
                              []> {
  let mayStore = 1;
}

// SETO - Set to ones (FFFF) - does NOT set status flags
// Always safe to rematerialize (produces constant 0xFFFF).
let isReMaterializable = 1, isAsCheapAsAMove = 1 in
def SETOr : Format3_Reg<0b0000011100, (outs GR16:$rs), (ins),
                        "SETO\t$rs",
                        [(set GR16:$rs, -1)]>;

def SETO_ind : Format3_IndReg<0b0000011100, (outs), (ins GR16:$rs),
                              "SETO\t*$rs",
                              []> {
  let mayStore = 1;
}

def SETO_sym : Format3_Sym<0b0000011100, (outs), (ins symaddr:$addr),
                           "SETO\t@$addr",
                           []> {
  let mayStore = 1;
}

def SETO_idx : Format3_Idx<0b0000011100, (outs),
                           (ins i16imm:$offset, IdxRegs:$ri),
                           "SETO\t@${offset}(${ri})",
                           []> {
  let mayStore = 1;
}

def SETO_pim : Format3_AutoInc<0b0000011100, (outs), (ins GR16:$rs),
                               "SETO\t*${rs}+",
                               []> {
  let mayStore = 1;
}

} // SchedRW = [WriteFast]

// SWPB - Swap bytes - does NOT affect status register (TMS9900 Data Manual)
let SchedRW = [WriteFast] in {
def SWPBr : Format3_Reg<0b0000011011, (outs GR16:$rd), (ins GR16:$rs),
                        "SWPB\t$rs",
                        [(set GR16:$rd, (bswap GR16:$rs))]> {
  let Constraints = "$rd = $rs";
}

def SWPB_ind : Format3_IndReg<0b0000011011, (outs), (ins GR16:$rs),
                              "SWPB\t*$rs",
                              []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def SWPB_sym : Format3_Sym<0b0000011011, (outs), (ins symaddr:$addr),
                           "SWPB\t@$addr",
                           []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def SWPB_idx : Format3_Idx<0b0000011011, (outs),
                           (ins i16imm:$offset, IdxRegs:$ri),
                           "SWPB\t@${offset}(${ri})",
                           []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def SWPB_pim : Format3_AutoInc<0b0000011011, (outs), (ins GR16:$rs),
                               "SWPB\t*${rs}+",
                               []> {
  let mayLoad = 1;
  let mayStore = 1;
}
} // SchedRW = [WriteFast]

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

// AB - Add Byte
def ABrr : Format1_Reg_TwoAddr<0b1010, 1, (outs GR16:$rd), (ins GR16:$rs1, GR16:$rs2),
                               "AB\t$rs2,$rd",
                               []> {
  let Constraints = "$rd = $rs1";
}

def Aim : Format1_IndLoad<0b1010, 0, (outs GR16:$rd), (ins GR16:$rs),
                          "A\t*$rs,$rd", []>;
def Aam : Format1_SymLoad<0b1010, 0, (outs GR16:$rd), (ins symaddr:$addr),
                          "A\t@$addr,$rd", []>;
def Axm : Format1_IdxLoad<0b1010, 0, (outs GR16:$rd),
                          (ins i16imm:$offset, IdxRegs:$ri),
                          "A\t@${offset}(${ri}),$rd", []>;
def Apim : Format1_AutoIncLoad<0b1010, 0, (outs GR16:$rd), (ins GR16:$rs),
                               "A\t*${rs}+,$rd", []>;
def Ami : Format1_IndStore<0b1010, 0, (outs), (ins GR16:$rd, GR16:$rs),
                           "A\t$rs,*$rd", []>;
def Ama : Format1_SymStore<0b1010, 0, (outs), (ins symaddr:$addr, GR16:$rs),
                           "A\t$rs,@$addr", []>;
def Amx : Format1_IdxStore<0b1010, 0, (outs),
                           (ins i16imm:$offset, IdxRegs:$ri, GR16:$rs),
                           "A\t$rs,@${offset}(${ri})", []>;
def Ampi : Format1_AutoIncStore<0b1010, 0, (outs), (ins GR16:$rd, GR16:$rs),
                                "A\t$rs,*${rd}+", []>;

def ABim : Format1_IndLoad<0b1010, 1, (outs GR16:$rd), (ins GR16:$rs),
                           "AB\t*$rs,$rd", []>;
def ABam : Format1_SymLoad<0b1010, 1, (outs GR16:$rd), (ins symaddr:$addr),
                           "AB\t@$addr,$rd", []>;
def ABxm : Format1_IdxLoad<0b1010, 1, (outs GR16:$rd),
                           (ins i16imm:$offset, IdxRegs:$ri),
                           "AB\t@${offset}(${ri}),$rd", []>;
def ABpim : Format1_AutoIncLoad<0b1010, 1, (outs GR16:$rd), (ins GR16:$rs),
                                "AB\t*${rs}+,$rd", []>;
def ABmi : Format1_IndStore<0b1010, 1, (outs), (ins GR16:$rd, GR16:$rs),
                            "AB\t$rs,*$rd", []>;
def ABma : Format1_SymStore<0b1010, 1, (outs), (ins symaddr:$addr, GR16:$rs),
                            "AB\t$rs,@$addr", []>;
def ABmx : Format1_IdxStore<0b1010, 1, (outs),
                            (ins i16imm:$offset, IdxRegs:$ri, GR16:$rs),
                            "AB\t$rs,@${offset}(${ri})", []>;
def ABmpi : Format1_AutoIncStore<0b1010, 1, (outs), (ins GR16:$rd, GR16:$rs),
                                 "AB\t$rs,*${rd}+", []>;

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

// SB - Subtract Byte
def SBrr : Format1_Reg_TwoAddr<0b0110, 1, (outs GR16:$rd), (ins GR16:$rs1, GR16:$rs2),
                               "SB\t$rs2,$rd",
                               []> {
  let Constraints = "$rd = $rs1";
}

def Sim : Format1_IndLoad<0b0110, 0, (outs GR16:$rd), (ins GR16:$rs),
                          "S\t*$rs,$rd", []>;
def Sam : Format1_SymLoad<0b0110, 0, (outs GR16:$rd), (ins symaddr:$addr),
                          "S\t@$addr,$rd", []>;
def Sxm : Format1_IdxLoad<0b0110, 0, (outs GR16:$rd),
                          (ins i16imm:$offset, IdxRegs:$ri),
                          "S\t@${offset}(${ri}),$rd", []>;
def Spim : Format1_AutoIncLoad<0b0110, 0, (outs GR16:$rd), (ins GR16:$rs),
                               "S\t*${rs}+,$rd", []>;
def Smi : Format1_IndStore<0b0110, 0, (outs), (ins GR16:$rd, GR16:$rs),
                           "S\t$rs,*$rd", []>;
def Sma : Format1_SymStore<0b0110, 0, (outs), (ins symaddr:$addr, GR16:$rs),
                           "S\t$rs,@$addr", []>;
def Smx : Format1_IdxStore<0b0110, 0, (outs),
                           (ins i16imm:$offset, IdxRegs:$ri, GR16:$rs),
                           "S\t$rs,@${offset}(${ri})", []>;
def Smpi : Format1_AutoIncStore<0b0110, 0, (outs), (ins GR16:$rd, GR16:$rs),
                                "S\t$rs,*${rd}+", []>;

def SBim : Format1_IndLoad<0b0110, 1, (outs GR16:$rd), (ins GR16:$rs),
                           "SB\t*$rs,$rd", []>;
def SBam : Format1_SymLoad<0b0110, 1, (outs GR16:$rd), (ins symaddr:$addr),
                           "SB\t@$addr,$rd", []>;
def SBxm : Format1_IdxLoad<0b0110, 1, (outs GR16:$rd),
                           (ins i16imm:$offset, IdxRegs:$ri),
                           "SB\t@${offset}(${ri}),$rd", []>;
def SBpim : Format1_AutoIncLoad<0b0110, 1, (outs GR16:$rd), (ins GR16:$rs),
                                "SB\t*${rs}+,$rd", []>;
def SBmi : Format1_IndStore<0b0110, 1, (outs), (ins GR16:$rd, GR16:$rs),
                            "SB\t$rs,*$rd", []>;
def SBma : Format1_SymStore<0b0110, 1, (outs), (ins symaddr:$addr, GR16:$rs),
                            "SB\t$rs,@$addr", []>;
def SBmx : Format1_IdxStore<0b0110, 1, (outs),
                            (ins i16imm:$offset, IdxRegs:$ri, GR16:$rs),
                            "SB\t$rs,@${offset}(${ri})", []>;
def SBmpi : Format1_AutoIncStore<0b0110, 1, (outs), (ins GR16:$rd, GR16:$rs),
                                 "SB\t$rs,*${rd}+", []>;

let SchedRW = [WriteFast] in {
// INC - Increment by 1
def INCr : Format3_Reg<0b0000010110, (outs GR16:$rd), (ins GR16:$rs),
                       "INC\t$rs",
                       [(set GR16:$rd, (add GR16:$rs, 1))]> {
  let Constraints = "$rd = $rs";
}

def INC_ind : Format3_IndReg<0b0000010110, (outs), (ins GR16:$rs),
                             "INC\t*$rs",
                             []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def INC_sym : Format3_Sym<0b0000010110, (outs), (ins symaddr:$addr),
                          "INC\t@$addr",
                          []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def INC_idx : Format3_Idx<0b0000010110, (outs),
                          (ins i16imm:$offset, IdxRegs:$ri),
                          "INC\t@${offset}(${ri})",
                          []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def INC_pim : Format3_AutoInc<0b0000010110, (outs), (ins GR16:$rs),
                              "INC\t*${rs}+",
                              []> {
  let mayLoad = 1;
  let mayStore = 1;
}

// INCT - Increment by 2
def INCTr : Format3_Reg<0b0000010111, (outs GR16:$rd), (ins GR16:$rs),
                        "INCT\t$rs",
                        [(set GR16:$rd, (add GR16:$rs, 2))]> {
  let Constraints = "$rd = $rs";
}

def INCT_ind : Format3_IndReg<0b0000010111, (outs), (ins GR16:$rs),
                              "INCT\t*$rs",
                              []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def INCT_sym : Format3_Sym<0b0000010111, (outs), (ins symaddr:$addr),
                           "INCT\t@$addr",
                           []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def INCT_idx : Format3_Idx<0b0000010111, (outs),
                           (ins i16imm:$offset, IdxRegs:$ri),
                           "INCT\t@${offset}(${ri})",
                           []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def INCT_pim : Format3_AutoInc<0b0000010111, (outs), (ins GR16:$rs),
                               "INCT\t*${rs}+",
                               []> {
  let mayLoad = 1;
  let mayStore = 1;
}

// DEC - Decrement by 1
def DECr : Format3_Reg<0b0000011000, (outs GR16:$rd), (ins GR16:$rs),
                       "DEC\t$rs",
                       [(set GR16:$rd, (sub GR16:$rs, 1))]> {
  let Constraints = "$rd = $rs";
}

def DEC_ind : Format3_IndReg<0b0000011000, (outs), (ins GR16:$rs),
                             "DEC\t*$rs",
                             []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def DEC_sym : Format3_Sym<0b0000011000, (outs), (ins symaddr:$addr),
                          "DEC\t@$addr",
                          []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def DEC_idx : Format3_Idx<0b0000011000, (outs),
                          (ins i16imm:$offset, IdxRegs:$ri),
                          "DEC\t@${offset}(${ri})",
                          []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def DEC_pim : Format3_AutoInc<0b0000011000, (outs), (ins GR16:$rs),
                              "DEC\t*${rs}+",
                              []> {
  let mayLoad = 1;
  let mayStore = 1;
}

// DECT - Decrement by 2
def DECTr : Format3_Reg<0b0000011001, (outs GR16:$rd), (ins GR16:$rs),
                        "DECT\t$rs",
                        [(set GR16:$rd, (sub GR16:$rs, 2))]> {
  let Constraints = "$rd = $rs";
}

def DECT_ind : Format3_IndReg<0b0000011001, (outs), (ins GR16:$rs),
                              "DECT\t*$rs",
                              []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def DECT_sym : Format3_Sym<0b0000011001, (outs), (ins symaddr:$addr),
                           "DECT\t@$addr",
                           []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def DECT_idx : Format3_Idx<0b0000011001, (outs),
                           (ins i16imm:$offset, IdxRegs:$ri),
                           "DECT\t@${offset}(${ri})",
                           []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def DECT_pim : Format3_AutoInc<0b0000011001, (outs), (ins GR16:$rs),
                               "DECT\t*${rs}+",
                               []> {
  let mayLoad = 1;
  let mayStore = 1;
}
}

// NEG - Negate (two's complement)
let SchedRW = [WriteImm12] in {
def NEGr : Format3_Reg<0b0000010100, (outs GR16:$rd), (ins GR16:$rs),
                       "NEG\t$rs",
                       [(set GR16:$rd, (ineg GR16:$rs))]> {
  let Constraints = "$rd = $rs";
}

def NEG_ind : Format3_IndReg<0b0000010100, (outs), (ins GR16:$rs),
                             "NEG\t*$rs",
                             []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def NEG_sym : Format3_Sym<0b0000010100, (outs), (ins symaddr:$addr),
                          "NEG\t@$addr",
                          []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def NEG_idx : Format3_Idx<0b0000010100, (outs),
                          (ins i16imm:$offset, IdxRegs:$ri),
                          "NEG\t@${offset}(${ri})",
                          []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def NEG_pim : Format3_AutoInc<0b0000010100, (outs), (ins GR16:$rs),
                              "NEG\t*${rs}+",
                              []> {
  let mayLoad = 1;
  let mayStore = 1;
}
}

// ABS - Absolute value
def ABSr : Format3_Reg<0b0000011101, (outs GR16:$rd), (ins GR16:$rs),
                       "ABS\t$rs",
                       [(set GR16:$rd, (abs GR16:$rs))]> {
  let Constraints = "$rd = $rs";
}

def ABS_ind : Format3_IndReg<0b0000011101, (outs), (ins GR16:$rs),
                             "ABS\t*$rs",
                             []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def ABS_sym : Format3_Sym<0b0000011101, (outs), (ins symaddr:$addr),
                          "ABS\t@$addr",
                          []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def ABS_idx : Format3_Idx<0b0000011101, (outs),
                          (ins i16imm:$offset, IdxRegs:$ri),
                          "ABS\t@${offset}(${ri})",
                          []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def ABS_pim : Format3_AutoInc<0b0000011101, (outs), (ins GR16:$rs),
                              "ABS\t*${rs}+",
                              []> {
  let mayLoad = 1;
  let mayStore = 1;
}
} // Defs = [ST]

//===----------------------------------------------------------------------===//
// Logical Instructions
// All logical instructions set status bits (LGT, AGT, EQ)
//===----------------------------------------------------------------------===//

let Defs = [ST] in {

// INV - Invert (one's complement)
let SchedRW = [WriteFast] in {
def INVr : Format3_Reg<0b0000010101, (outs GR16:$rd), (ins GR16:$rs),
                       "INV\t$rs",
                       [(set GR16:$rd, (not GR16:$rs))]> {
  let Constraints = "$rd = $rs";
}

def INV_ind : Format3_IndReg<0b0000010101, (outs), (ins GR16:$rs),
                             "INV\t*$rs",
                             []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def INV_sym : Format3_Sym<0b0000010101, (outs), (ins symaddr:$addr),
                          "INV\t@$addr",
                          []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def INV_idx : Format3_Idx<0b0000010101, (outs),
                          (ins i16imm:$offset, IdxRegs:$ri),
                          "INV\t@${offset}(${ri})",
                          []> {
  let mayLoad = 1;
  let mayStore = 1;
}

def INV_pim : Format3_AutoInc<0b0000010101, (outs), (ins GR16:$rs),
                              "INV\t*${rs}+",
                              []> {
  let mayLoad = 1;
  let mayStore = 1;
}
}
// SOC - Set Ones Corresponding (OR)
def SOCrr : Format1_Reg_TwoAddr<0b1110, 0, (outs GR16:$rd), (ins GR16:$rs1, GR16:$rs2),
                                "SOC\t$rs2,$rd",
                                [(set GR16:$rd, (or GR16:$rs1, GR16:$rs2))]> {
  let Constraints = "$rd = $rs1";
}

// SOCB - Set Ones Corresponding Byte
def SOCBrr : Format1_Reg_TwoAddr<0b1110, 1, (outs GR16:$rd), (ins GR16:$rs1, GR16:$rs2),
                                 "SOCB\t$rs2,$rd",
                                 []> {
  let Constraints = "$rd = $rs1";
}

def SOCim : Format1_IndLoad<0b1110, 0, (outs GR16:$rd), (ins GR16:$rs),
                            "SOC\t*$rs,$rd", []>;
def SOCam : Format1_SymLoad<0b1110, 0, (outs GR16:$rd), (ins symaddr:$addr),
                            "SOC\t@$addr,$rd", []>;
def SOCxm : Format1_IdxLoad<0b1110, 0, (outs GR16:$rd),
                            (ins i16imm:$offset, IdxRegs:$ri),
                            "SOC\t@${offset}(${ri}),$rd", []>;
def SOCpim : Format1_AutoIncLoad<0b1110, 0, (outs GR16:$rd), (ins GR16:$rs),
                                 "SOC\t*${rs}+,$rd", []>;
def SOCmi : Format1_IndStore<0b1110, 0, (outs), (ins GR16:$rd, GR16:$rs),
                             "SOC\t$rs,*$rd", []>;
def SOCma : Format1_SymStore<0b1110, 0, (outs), (ins symaddr:$addr, GR16:$rs),
                             "SOC\t$rs,@$addr", []>;
def SOCmx : Format1_IdxStore<0b1110, 0, (outs),
                             (ins i16imm:$offset, IdxRegs:$ri, GR16:$rs),
                             "SOC\t$rs,@${offset}(${ri})", []>;
def SOCmpi : Format1_AutoIncStore<0b1110, 0, (outs), (ins GR16:$rd, GR16:$rs),
                                  "SOC\t$rs,*${rd}+", []>;

def SOCBim : Format1_IndLoad<0b1110, 1, (outs GR16:$rd), (ins GR16:$rs),
                             "SOCB\t*$rs,$rd", []>;
def SOCBam : Format1_SymLoad<0b1110, 1, (outs GR16:$rd), (ins symaddr:$addr),
                             "SOCB\t@$addr,$rd", []>;
def SOCBxm : Format1_IdxLoad<0b1110, 1, (outs GR16:$rd),
                             (ins i16imm:$offset, IdxRegs:$ri),
                             "SOCB\t@${offset}(${ri}),$rd", []>;
def SOCBpim : Format1_AutoIncLoad<0b1110, 1, (outs GR16:$rd), (ins GR16:$rs),
                                  "SOCB\t*${rs}+,$rd", []>;
def SOCBmi : Format1_IndStore<0b1110, 1, (outs), (ins GR16:$rd, GR16:$rs),
                              "SOCB\t$rs,*$rd", []>;
def SOCBma : Format1_SymStore<0b1110, 1, (outs), (ins symaddr:$addr, GR16:$rs),
                              "SOCB\t$rs,@$addr", []>;
def SOCBmx : Format1_IdxStore<0b1110, 1, (outs),
                              (ins i16imm:$offset, IdxRegs:$ri, GR16:$rs),
                              "SOCB\t$rs,@${offset}(${ri})", []>;
def SOCBmpi : Format1_AutoIncStore<0b1110, 1, (outs), (ins GR16:$rd, GR16:$rs),
                                   "SOCB\t$rs,*${rd}+", []>;

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

// SZCB - Set Zeros Corresponding Byte
def SZCBrr : Format1_Reg_TwoAddr<0b0100, 1, (outs GR16:$rd), (ins GR16:$rs1, GR16:$rs2),
                                 "SZCB\t$rs2,$rd",
                                 []> {
  let Constraints = "$rd = $rs1";
}

def SZCim : Format1_IndLoad<0b0100, 0, (outs GR16:$rd), (ins GR16:$rs),
                            "SZC\t*$rs,$rd", []>;
def SZCam : Format1_SymLoad<0b0100, 0, (outs GR16:$rd), (ins symaddr:$addr),
                            "SZC\t@$addr,$rd", []>;
def SZCxm : Format1_IdxLoad<0b0100, 0, (outs GR16:$rd),
                            (ins i16imm:$offset, IdxRegs:$ri),
                            "SZC\t@${offset}(${ri}),$rd", []>;
def SZCpim : Format1_AutoIncLoad<0b0100, 0, (outs GR16:$rd), (ins GR16:$rs),
                                 "SZC\t*${rs}+,$rd", []>;
def SZCmi : Format1_IndStore<0b0100, 0, (outs), (ins GR16:$rd, GR16:$rs),
                             "SZC\t$rs,*$rd", []>;
def SZCma : Format1_SymStore<0b0100, 0, (outs), (ins symaddr:$addr, GR16:$rs),
                             "SZC\t$rs,@$addr", []>;
def SZCmx : Format1_IdxStore<0b0100, 0, (outs),
                             (ins i16imm:$offset, IdxRegs:$ri, GR16:$rs),
                             "SZC\t$rs,@${offset}(${ri})", []>;
def SZCmpi : Format1_AutoIncStore<0b0100, 0, (outs), (ins GR16:$rd, GR16:$rs),
                                  "SZC\t$rs,*${rd}+", []>;

def SZCBim : Format1_IndLoad<0b0100, 1, (outs GR16:$rd), (ins GR16:$rs),
                             "SZCB\t*$rs,$rd", []>;
def SZCBam : Format1_SymLoad<0b0100, 1, (outs GR16:$rd), (ins symaddr:$addr),
                             "SZCB\t@$addr,$rd", []>;
def SZCBxm : Format1_IdxLoad<0b0100, 1, (outs GR16:$rd),
                             (ins i16imm:$offset, IdxRegs:$ri),
                             "SZCB\t@${offset}(${ri}),$rd", []>;
def SZCBpim : Format1_AutoIncLoad<0b0100, 1, (outs GR16:$rd), (ins GR16:$rs),
                                  "SZCB\t*${rs}+,$rd", []>;
def SZCBmi : Format1_IndStore<0b0100, 1, (outs), (ins GR16:$rd, GR16:$rs),
                              "SZCB\t$rs,*$rd", []>;
def SZCBma : Format1_SymStore<0b0100, 1, (outs), (ins symaddr:$addr, GR16:$rs),
                              "SZCB\t$rs,@$addr", []>;
def SZCBmx : Format1_IdxStore<0b0100, 1, (outs),
                              (ins i16imm:$offset, IdxRegs:$ri, GR16:$rs),
                              "SZCB\t$rs,@${offset}(${ri})", []>;
def SZCBmpi : Format1_AutoIncStore<0b0100, 1, (outs), (ins GR16:$rd, GR16:$rs),
                                   "SZCB\t$rs,*${rd}+", []>;

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

// COC/CZC - Compare Ones/Zeros Corresponding
def COCrr : Format2_Reg<0b001000, (outs), (ins GR16:$rd, GR16:$rs),
                        "COC\t$rs,$rd",
                        []>;
def CZCrr : Format2_Reg<0b001001, (outs), (ins GR16:$rd, GR16:$rs),
                        "CZC\t$rs,$rd",
                        []>;

def XORim : Format2_Ind<0b001010, (outs GR16:$rd), (ins GR16:$rs),
                        "XOR\t*$rs,$rd",
                        []>;
def XORam : Format2_Sym<0b001010, (outs GR16:$rd), (ins symaddr:$addr),
                        "XOR\t@$addr,$rd",
                        []>;
def XORxm : Format2_Idx<0b001010, (outs GR16:$rd),
                        (ins i16imm:$offset, IdxRegs:$ri),
                        "XOR\t@${offset}(${ri}),$rd",
                        []>;
def XORpim : Format2_AutoInc<0b001010, (outs GR16:$rd), (ins GR16:$rs),
                             "XOR\t*${rs}+,$rd",
                             []>;

def COCim : Format2_Ind<0b001000, (outs), (ins GR16:$rd, GR16:$rs),
                        "COC\t*$rs,$rd",
                        []>;
def COCam : Format2_Sym<0b001000, (outs), (ins GR16:$rd, symaddr:$addr),
                        "COC\t@$addr,$rd",
                        []>;
def COCxm : Format2_Idx<0b001000, (outs),
                        (ins GR16:$rd, i16imm:$offset, IdxRegs:$ri),
                        "COC\t@${offset}(${ri}),$rd",
                        []>;
def COCpim : Format2_AutoInc<0b001000, (outs), (ins GR16:$rd, GR16:$rs),
                             "COC\t*${rs}+,$rd",
                             []>;

def CZCim : Format2_Ind<0b001001, (outs), (ins GR16:$rd, GR16:$rs),
                        "CZC\t*$rs,$rd",
                        []>;
def CZCam : Format2_Sym<0b001001, (outs), (ins GR16:$rd, symaddr:$addr),
                        "CZC\t@$addr,$rd",
                        []>;
def CZCxm : Format2_Idx<0b001001, (outs),
                        (ins GR16:$rd, i16imm:$offset, IdxRegs:$ri),
                        "CZC\t@${offset}(${ri}),$rd",
                        []>;
def CZCpim : Format2_AutoInc<0b001001, (outs), (ins GR16:$rd, GR16:$rs),
                             "CZC\t*${rs}+,$rd",
                             []>;

} // Defs = [ST]

//===----------------------------------------------------------------------===//
// Shift Instructions
// All shift instructions set status bits (LGT, AGT, EQ, C)
//===----------------------------------------------------------------------===//

let Defs = [ST] in {

// SLA - Shift Left Arithmetic
def SLAri : Format7_TwoAddr<0b00001010, (outs GR16:$rd), (ins GR16:$rs, shcount:$cnt),
                            "SLA\t$rs,$cnt",
                            [(set GR16:$rd, (shl GR16:$rs, (i16 imm:$cnt)))]> {
  let Constraints = "$rd = $rs";
}

// SRA - Shift Right Arithmetic
def SRAri : Format7_TwoAddr<0b00001000, (outs GR16:$rd), (ins GR16:$rs, shcount:$cnt),
                            "SRA\t$rs,$cnt",
                            [(set GR16:$rd, (sra GR16:$rs, (i16 imm:$cnt)))]> {
  let Constraints = "$rd = $rs";
}

// SRL - Shift Right Logical
def SRLri : Format7_TwoAddr<0b00001001, (outs GR16:$rd), (ins GR16:$rs, shcount:$cnt),
                            "SRL\t$rs,$cnt",
                            [(set GR16:$rd, (srl GR16:$rs, (i16 imm:$cnt)))]> {
  let Constraints = "$rd = $rs";
}

// SRC - Shift Right Circular (rotate right)
def SRCri : Format7_TwoAddr<0b00001011, (outs GR16:$rd), (ins GR16:$rs, shcount:$cnt),
                            "SRC\t$rs,$cnt",
                            [(set GR16:$rd, (rotr GR16:$rs, (i16 imm:$cnt)))]> {
  let Constraints = "$rd = $rs";
}

} // Defs = [ST]

// Shift instructions with count from R0 (count field = 0)
// These are used by variable shift pseudos
let Defs = [ST], isCodeGenOnly = 1 in {
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

let usesCustomInserter = 1, Defs = [R0, ST] in {
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
def CBrr : Format1_Reg_Cmp<0b1000, 1, (outs), (ins GR16:$rs1, GR16:$rs2),
                           "CB\t$rs1,$rs2",
                           []>;

def Cim : Format1_IndLoad<0b1000, 0, (outs), (ins GR16:$rd, GR16:$rs),
                          "C\t*$rs,$rd", []>;
def Cam : Format1_SymLoad<0b1000, 0, (outs), (ins GR16:$rd, symaddr:$addr),
                          "C\t@$addr,$rd", []>;
def Cxm : Format1_IdxLoad<0b1000, 0, (outs),
                          (ins GR16:$rd, i16imm:$offset, IdxRegs:$ri),
                          "C\t@${offset}(${ri}),$rd", []>;
def Cpim : Format1_AutoIncLoad<0b1000, 0, (outs), (ins GR16:$rd, GR16:$rs),
                               "C\t*${rs}+,$rd", []>;
def Cmi : Format1_IndStore<0b1000, 0, (outs), (ins GR16:$rd, GR16:$rs),
                           "C\t$rs,*$rd", []>;
def Cma : Format1_SymStore<0b1000, 0, (outs), (ins symaddr:$addr, GR16:$rs),
                           "C\t$rs,@$addr", []>;
def Cmx : Format1_IdxStore<0b1000, 0, (outs),
                           (ins i16imm:$offset, IdxRegs:$ri, GR16:$rs),
                           "C\t$rs,@${offset}(${ri})", []>;
def Cmpi : Format1_AutoIncStore<0b1000, 0, (outs), (ins GR16:$rd, GR16:$rs),
                                "C\t$rs,*${rd}+", []>;

def CBim : Format1_IndLoad<0b1000, 1, (outs), (ins GR16:$rd, GR16:$rs),
                           "CB\t*$rs,$rd", []>;
def CBam : Format1_SymLoad<0b1000, 1, (outs), (ins GR16:$rd, symaddr:$addr),
                           "CB\t@$addr,$rd", []>;
def CBxm : Format1_IdxLoad<0b1000, 1, (outs),
                           (ins GR16:$rd, i16imm:$offset, IdxRegs:$ri),
                           "CB\t@${offset}(${ri}),$rd", []>;
def CBpim : Format1_AutoIncLoad<0b1000, 1, (outs), (ins GR16:$rd, GR16:$rs),
                                "CB\t*${rs}+,$rd", []>;
def CBmi : Format1_IndStore<0b1000, 1, (outs), (ins GR16:$rd, GR16:$rs),
                            "CB\t$rs,*$rd", []>;
def CBma : Format1_SymStore<0b1000, 1, (outs), (ins symaddr:$addr, GR16:$rs),
                            "CB\t$rs,@$addr", []>;
def CBmx : Format1_IdxStore<0b1000, 1, (outs),
                            (ins i16imm:$offset, IdxRegs:$ri, GR16:$rs),
                            "CB\t$rs,@${offset}(${ri})", []>;
def CBmpi : Format1_AutoIncStore<0b1000, 1, (outs), (ins GR16:$rd, GR16:$rs),
                                 "CB\t$rs,*${rd}+", []>;

// CI - Compare Immediate (subop=8)
def CI : Format8_Cmp<0b1000, (outs), (ins GR16:$rs, i16imm:$imm),
                     "CI\t$rs,$imm",
                 [(TMS9900cmp GR16:$rs, imm:$imm)]>;
}

//===----------------------------------------------------------------------===//
// Compare + Branch Pseudos (to keep flags adjacent)
//===----------------------------------------------------------------------===//

let isCodeGenOnly = 1, isPseudo = 1, isBranch = 1, isTerminator = 1 in {
def CMPBRrr : Pseudo<(outs),
                     (ins GR16:$lhs, GR16:$rhs, i16imm:$cc, brtarget8:$target),
                     "# CMPBRrr $lhs, $rhs, $cc, $target",
                     []>;
def CMPBRri : Pseudo<(outs),
                     (ins GR16:$lhs, i16imm:$rhs, i16imm:$cc, brtarget8:$target),
                     "# CMPBRri $lhs, $rhs, $cc, $target",
                     []>;
}

//===----------------------------------------------------------------------===//
// Branch and Jump Instructions
//===----------------------------------------------------------------------===//

// B - Unconditional Branch (single operand format, register indirect)
let SchedRW = [WriteBranch] in {
def Br : Format3_IndReg<0b0000010001, (outs), (ins GR16:$rs),
                        "B\t*$rs",
                        [(brind GR16:$rs)]> {
  let isBranch = 1;
  let isTerminator = 1;
  let isBarrier = 1;
  let isIndirectBranch = 1;
}

// B with register direct addressing (rare, but valid)
def Br_reg : Format3_Reg<0b0000010001, (outs), (ins GR16:$rs),
                         "B\t$rs",
                         []> {
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

// B with calltarget (for tail call optimization)
// Same encoding as B_sym but takes a calltarget operand (function address)
// instead of brtarget16 (basic block label).
let isCall = 1, isReturn = 1, isTerminator = 1, isBarrier = 1 in
def TAIL_B : TMS9900Inst<(outs), (ins calltarget:$target),
                         "B\t@$target",
                         []> {
  bits<16> target;
  bits<32> Inst;
  let Inst{15-6} = 0b0000010001;  // B opcode (10 bits)
  let Inst{5-4} = 0b10;           // Ts = symbolic addressing
  let Inst{3-0} = 0b0000;         // S = 0 (address follows)
  let Inst{31-16} = target;       // Second word: target address
  let Size = 4;
  let isCodeGenOnly = 1;
  let SchedRW = [WriteBranch];
}
}

// JMP - Unconditional jump (PC-relative)
def JMP : Format6<0b00010000, (outs), (ins brtarget8:$disp),
                  "JMP\t$disp",
                  [(br bb:$disp)]> {
  let isBarrier = 1;
}

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
let SchedRW = [WriteCall] in {
def BL : Format3_IndReg<0b0000011010, (outs), (ins GR16:$rs),
                        "BL\t*$rs",
                        [(TMS9900call GR16:$rs)]> {
  let isCall = 1;
  let Defs = [R11];
}

// BL with register direct addressing (rare, but valid)
def BL_reg : Format3_Reg<0b0000011010, (outs), (ins GR16:$rs),
                         "BL\t$rs",
                         []> {
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
}

// BLWP - Branch and Load Workspace Pointer (general addressing)
let SchedRW = [WriteControl26] in {
def BLWP : Format3_IndReg<0b0000010000, (outs), (ins GR16:$rs),
                          "BLWP\t*$rs",
                          []> {
  let isCall = 1;
}

def BLWP_reg : Format3_Reg<0b0000010000, (outs), (ins GR16:$rs),
                           "BLWP\t$rs",
                           []> {
  let isCall = 1;
}

def BLWP_sym : Format3_Sym<0b0000010000, (outs), (ins symaddr:$addr),
                           "BLWP\t@$addr",
                           []> {
  let isCall = 1;
}

def BLWP_idx : Format3_Idx<0b0000010000, (outs),
                          (ins i16imm:$offset, IdxRegs:$ri),
                           "BLWP\t@${offset}(${ri})",
                           []> {
  let isCall = 1;
}

def BLWP_pim : Format3_AutoInc<0b0000010000, (outs), (ins GR16:$rs),
                               "BLWP\t*${rs}+",
                               []> {
  let isCall = 1;
}
}

// X - Execute instruction (general addressing)
def X : Format3_IndReg<0b0000010010, (outs), (ins GR16:$rs),
                       "X\t*$rs",
                       []> {
  let SchedRW = [WriteFast8];
}

def X_reg : Format3_Reg<0b0000010010, (outs), (ins GR16:$rs),
                        "X\t$rs",
                        []>;

def X_sym : Format3_Sym<0b0000010010, (outs), (ins symaddr:$addr),
                        "X\t@$addr",
                        []>;

def X_idx : Format3_Idx<0b0000010010, (outs),
                        (ins i16imm:$offset, IdxRegs:$ri),
                        "X\t@${offset}(${ri})",
                        []>;

def X_pim : Format3_AutoInc<0b0000010010, (outs), (ins GR16:$rs),
                            "X\t*${rs}+",
                            []>;

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
  let SchedRW = [WriteBranch];
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
// The destination register is encoded in bits 9-6 of the instruction word.
// We still hardcode Uses/Defs to R0:R1 since the pseudo expanders always
// target R0, but the encoding now correctly encodes the $rd operand.
let SchedRW = [WriteSlow] in {
let Uses = [R0], Defs = [R0, R1] in
def MPYrr : Format2_Reg<0b001110, (outs), (ins GR16:$rs, GR16:$rd),
                         "MPY\t$rs,$rd",
                         []>;
let Uses = [R0], Defs = [R0, R1] in
def MPYim : Format2_Ind<0b001110, (outs), (ins GR16:$rs, GR16:$rd),
                         "MPY\t*$rs,$rd",
                         []>;
let Uses = [R0], Defs = [R0, R1] in
def MPYam : Format2_Sym<0b001110, (outs), (ins symaddr:$addr, GR16:$rd),
                         "MPY\t@$addr,$rd",
                         []>;
let Uses = [R0], Defs = [R0, R1] in
def MPYxm : Format2_Idx<0b001110, (outs),
                         (ins i16imm:$offset, IdxRegs:$ri, GR16:$rd),
                         "MPY\t@${offset}(${ri}),$rd",
                         []>;
let Uses = [R0], Defs = [R0, R1] in
def MPYpim : Format2_AutoInc<0b001110, (outs), (ins GR16:$rs, GR16:$rd),
                              "MPY\t*${rs}+,$rd",
                              []>;
}

// DIV - Divide (unsigned 32/16 -> 16q, 16r)
// Dividend in Rd:Rd+1 (32-bit input), divisor in Rs
// Result: quotient in Rd, remainder in Rd+1
// Note: Rd must be an even register
// The destination register is encoded in bits 9-6 of the instruction word.
let SchedRW = [WriteDiv] in {
let Uses = [R0, R1], Defs = [R0, R1] in
def DIVrr : Format2_Reg<0b001111, (outs), (ins GR16:$rs, GR16:$rd),
                         "DIV\t$rs,$rd",
                         []>;
let Uses = [R0, R1], Defs = [R0, R1] in
def DIVim : Format2_Ind<0b001111, (outs), (ins GR16:$rs, GR16:$rd),
                         "DIV\t*$rs,$rd",
                         []>;
let Uses = [R0, R1], Defs = [R0, R1] in
def DIVam : Format2_Sym<0b001111, (outs), (ins symaddr:$addr, GR16:$rd),
                         "DIV\t@$addr,$rd",
                         []>;
let Uses = [R0, R1], Defs = [R0, R1] in
def DIVxm : Format2_Idx<0b001111, (outs),
                         (ins i16imm:$offset, IdxRegs:$ri, GR16:$rd),
                         "DIV\t@${offset}(${ri}),$rd",
                         []>;
let Uses = [R0, R1], Defs = [R0, R1] in
def DIVpim : Format2_AutoInc<0b001111, (outs), (ins GR16:$rs, GR16:$rd),
                              "DIV\t*${rs}+,$rd",
                              []>;
}

//===----------------------------------------------------------------------===//
// XOP (Extended Operation)
//===----------------------------------------------------------------------===//

def XOPr : FormatXOP_Reg<0b001011, (outs), (ins GR16:$rs, imm4:$vec),
                         "XOP\t$rs,$vec",
                         []>;

def XOPi : FormatXOP_Ind<0b001011, (outs), (ins GR16:$rs, imm4:$vec),
                         "XOP\t*$rs,$vec",
                         []>;

def XOPpim : FormatXOP_AutoInc<0b001011, (outs), (ins GR16:$rs, imm4:$vec),
                               "XOP\t*${rs}+,$vec",
                               []>;

def XOPam : FormatXOP_Sym<0b001011, (outs), (ins symaddr:$addr, imm4:$vec),
                          "XOP\t@$addr,$vec",
                          []>;

def XOPxm : FormatXOP_Idx<0b001011, (outs),
                          (ins i16imm:$offset, IdxRegs:$ri, imm4:$vec),
                          "XOP\t@${offset}(${ri}),$vec",
                          []>;

//===----------------------------------------------------------------------===//
// CRU Multi-bit Instructions (LDCR/STCR)
//===----------------------------------------------------------------------===//

let Uses = [R12] in {
def LDCRr : Format4_Reg<0b001100, (outs), (ins GR16:$rs, cru_count:$cnt),
                        "LDCR\t$rs,$cnt",
                        []>;
def LDCRi : Format4_Ind<0b001100, (outs), (ins GR16:$rs, cru_count:$cnt),
                        "LDCR\t*$rs,$cnt",
                        []>;
def LDCRpim : Format4_AutoInc<0b001100, (outs), (ins GR16:$rs, cru_count:$cnt),
                              "LDCR\t*${rs}+,$cnt",
                              []>;
def LDCRam : Format4_Sym<0b001100, (outs), (ins symaddr:$addr, cru_count:$cnt),
                         "LDCR\t@$addr,$cnt",
                         []>;
def LDCRxm : Format4_Idx<0b001100, (outs),
                         (ins i16imm:$offset, IdxRegs:$ri, cru_count:$cnt),
                         "LDCR\t@${offset}(${ri}),$cnt",
                         []>;

let SchedRW = [WriteCRU] in {
def STCRr : Format4_Reg<0b001101, (outs), (ins GR16:$rs, cru_count:$cnt),
                        "STCR\t$rs,$cnt",
                        []>;
def STCRi : Format4_Ind<0b001101, (outs), (ins GR16:$rs, cru_count:$cnt),
                        "STCR\t*$rs,$cnt",
                        []>;
def STCRpim : Format4_AutoInc<0b001101, (outs), (ins GR16:$rs, cru_count:$cnt),
                              "STCR\t*${rs}+,$cnt",
                              []>;
def STCRam : Format4_Sym<0b001101, (outs), (ins symaddr:$addr, cru_count:$cnt),
                         "STCR\t@$addr,$cnt",
                         []>;
def STCRxm : Format4_Idx<0b001101, (outs),
                         (ins i16imm:$offset, IdxRegs:$ri, cru_count:$cnt),
                         "STCR\t@${offset}(${ri}),$cnt",
                         []>;
}
} // Uses = [R12]

//===----------------------------------------------------------------------===//
// CRU Single-bit Instructions (SBO/SBZ/TB)
//===----------------------------------------------------------------------===//

let Uses = [R12] in {
def SBO : Format5<0b00011101, (outs), (ins cru_disp8:$disp),
                  "SBO\t$disp",
                  []>;
def SBZ : Format5<0b00011110, (outs), (ins cru_disp8:$disp),
                  "SBZ\t$disp",
                  []>;
def TB : Format5<0b00011111, (outs), (ins cru_disp8:$disp),
                 "TB\t$disp",
                 []> {
  let Defs = [ST];
}
} // Uses = [R12]

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
  let SchedRW = [WriteFast];
}

// LIMI - Load Interrupt Mask Immediate
def LIMI : Format9<0b00000011000, (outs), (ins i16imm:$imm),
                   "LIMI\t$imm",
                   []> {
  let SchedRW = [WriteControl16];
}

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
  let SchedRW = [WriteFast8];
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
  let SchedRW = [WriteFast8];
}

//===----------------------------------------------------------------------===//
// External Instructions
//===----------------------------------------------------------------------===//

let SchedRW = [WriteImm12] in {
def IDLE : Format11<0b0000001101000000, (outs), (ins),
                    "IDLE",
                    []>;

def RSET : Format11<0b0000001101100000, (outs), (ins),
                    "RSET",
                    []>;

def CKOF : Format11<0b0000001111000000, (outs), (ins),
                    "CKOF",
                    []>;

def CKON : Format11<0b0000001110100000, (outs), (ins),
                    "CKON",
                    []>;

def LREX : Format11<0b0000001111100000, (outs), (ins),
                    "LREX",
                    []>;
}

//===----------------------------------------------------------------------===//
// NOP (alias for JMP $+2)
//===----------------------------------------------------------------------===//

def : InstAlias<"NOP", (JMP 0)>;

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

// Indexed addressing for global addresses: MOV @global(Rx),Rd
// Folds (LI Rt,global / A Rx,Rt / MOV *Rt,Rd) into (MOV @global(Rx),Rd)
// Saves 2 words + ~8 cycles. R0 excluded by IdxRegs constraint on MOVxm.
def : Pat<(load (add (TMS9900Wrapper tglobaladdr:$addr), GR16:$idx)),
          (MOVxm tglobaladdr:$addr, GR16:$idx)>;

// Indexed store for global addresses: MOV Rs,@global(Rx)
def : Pat<(store GR16:$val, (add (TMS9900Wrapper tglobaladdr:$addr), GR16:$idx)),
          (MOVmx tglobaladdr:$addr, GR16:$idx, GR16:$val)>;

// Indexed byte load for global addresses: MOVB @global(Rx),Rd
def : Pat<(TMS9900byteload (add (TMS9900Wrapper tglobaladdr:$addr), GR16:$idx)),
          (MOVBxm tglobaladdr:$addr, GR16:$idx)>;

// Indexed byte store for global addresses: MOVB Rs,@global(Rx)
def : Pat<(TMS9900bytestore GR16:$val, (add (TMS9900Wrapper tglobaladdr:$addr), GR16:$idx)),
          (MOVBmx tglobaladdr:$addr, GR16:$idx, GR16:$val)>;
