//===-- TMS9900ISelLowering.cpp - TMS9900 DAG Lowering Implementation -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TMS9900TargetLowering class.
//
//===----------------------------------------------------------------------===//

#include "TMS9900ISelLowering.h"
#include "TMS9900.h"
#include "TMS9900MachineFunctionInfo.h"
#include "TMS9900Subtarget.h"
#include "TMS9900TargetMachine.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/RuntimeLibcalls.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"

using namespace llvm;

#define DEBUG_TYPE "tms9900-lower"

#include "TMS9900GenCallingConv.inc"

TMS9900TargetLowering::TMS9900TargetLowering(const TargetMachine &TM,
                                               const TMS9900Subtarget &STI)
    : TargetLowering(TM), Subtarget(STI) {

  // Set up the register classes.
  addRegisterClass(MVT::i16, &TMS9900::GR16RegClass);
  // Note: We don't register i32 as a legal type - LLVM will automatically
  // split i32 operations into pairs of i16 operations where possible,
  // or use libcalls for complex operations.

  // Compute derived properties from the register classes.
  computeRegisterProperties(STI.getRegisterInfo());

  // ===== 8-bit (i8) Operations =====
  // TMS9900 has byte instructions (MOVB, AB, SB, etc.) but they operate
  // on the upper byte of registers. We promote i8 to i16 for most operations.
  // Byte loads/stores need custom lowering because MOVB uses the HIGH byte
  // of a register, but LLVM puts byte values in the LOW byte.

  // i8 loads and stores need custom lowering for byte position
  // TMS9900 MOVB uses the HIGH byte of a register, so we need to shift
  setTruncStoreAction(MVT::i16, MVT::i8, Custom);
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i16, MVT::i8, Custom);
  setLoadExtAction(ISD::SEXTLOAD, MVT::i16, MVT::i8, Custom);
  setLoadExtAction(ISD::EXTLOAD, MVT::i16, MVT::i8, Custom);

  // i1 loads need to promote to i8 (then custom-lowered to i16)
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i16, MVT::i1, Promote);
  setLoadExtAction(ISD::SEXTLOAD, MVT::i16, MVT::i1, Promote);
  setLoadExtAction(ISD::EXTLOAD, MVT::i16, MVT::i1, Promote);

  // Also mark i8 operations as needing promotion to i16, which will then
  // go through our truncating store lowering
  setOperationAction(ISD::STORE, MVT::i8, Promote);

  // Promote i8 operations to i16
  setOperationAction(ISD::ADD, MVT::i8, Promote);
  setOperationAction(ISD::SUB, MVT::i8, Promote);
  setOperationAction(ISD::AND, MVT::i8, Promote);
  setOperationAction(ISD::OR, MVT::i8, Promote);
  setOperationAction(ISD::XOR, MVT::i8, Promote);
  setOperationAction(ISD::SHL, MVT::i8, Promote);
  setOperationAction(ISD::SRA, MVT::i8, Promote);
  setOperationAction(ISD::SRL, MVT::i8, Promote);
  setOperationAction(ISD::MUL, MVT::i8, Promote);
  setOperationAction(ISD::SDIV, MVT::i8, Promote);
  setOperationAction(ISD::UDIV, MVT::i8, Promote);
  setOperationAction(ISD::SREM, MVT::i8, Promote);
  setOperationAction(ISD::UREM, MVT::i8, Promote);

  // Extensions
  setOperationAction(ISD::SIGN_EXTEND, MVT::i16, Legal);
  setOperationAction(ISD::ZERO_EXTEND, MVT::i16, Legal);
  setOperationAction(ISD::ANY_EXTEND, MVT::i16, Legal);
  setOperationAction(ISD::TRUNCATE, MVT::i8, Legal);

  // Post-increment addressing modes for i16 and i8
  // TMS9900 supports *R+ for auto-increment (post-inc by 2 for words, 1 for bytes)
  setIndexedLoadAction(ISD::POST_INC, MVT::i16, Legal);
  setIndexedStoreAction(ISD::POST_INC, MVT::i16, Legal);
  setIndexedLoadAction(ISD::POST_INC, MVT::i8, Legal);
  setIndexedStoreAction(ISD::POST_INC, MVT::i8, Legal);

  // Set scheduling preference
  setSchedulingPreference(Sched::Source);

  // Set stack pointer register
  setStackPointerRegisterToSaveRestore(TMS9900::R10);

  // TMS9900 is big-endian
  // Use all-ones for true so bitwise boolean expansions (AND/OR/INV)
  // remain correct in integer legalization.
  setBooleanContents(ZeroOrNegativeOneBooleanContent);
  setBooleanVectorContents(ZeroOrNegativeOneBooleanContent);

  // TMS9900 has no atomic instructions - it's a simple single-core CPU
  // with no caches or memory barriers. All memory operations are inherently
  // ordered and visible. The shouldExpandAtomic*InIR() methods in the header
  // tell LLVM to convert all atomics to regular memory operations.
  // We set max atomic size to 16 bits (our word size) so LLVM knows we
  // can handle word-sized operations (after conversion to non-atomic).
  setMaxAtomicSizeInBitsSupported(16);

  // Division and remainder
  // All use native DIV instruction via pseudo instructions
  // Signed versions handle sign correction in the custom inserter
  setOperationAction(ISD::SDIV, MVT::i16, Legal);  // Uses SDIV16 pseudo
  setOperationAction(ISD::UDIV, MVT::i16, Legal);  // Uses UDIV16 pseudo
  setOperationAction(ISD::SREM, MVT::i16, Legal);  // Uses SREM16 pseudo
  setOperationAction(ISD::UREM, MVT::i16, Legal);  // Uses UREM16 pseudo
  setOperationAction(ISD::SDIVREM, MVT::i16, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i16, Expand);

  // Multiplication - we have MPY (16x16->32, result in Rd:Rd+1)
  // Uses MUL16 pseudo instruction that expands via custom inserter
  setOperationAction(ISD::MUL, MVT::i16, Legal);  // Uses MUL16 pseudo
  setOperationAction(ISD::MULHS, MVT::i16, Expand);
  setOperationAction(ISD::MULHU, MVT::i16, Expand);  // Could implement later
  setOperationAction(ISD::SMUL_LOHI, MVT::i16, Expand);
  setOperationAction(ISD::UMUL_LOHI, MVT::i16, Expand);

  // No direct support for these
  setOperationAction(ISD::CTTZ, MVT::i16, Expand);
  setOperationAction(ISD::CTLZ, MVT::i16, Expand);
  setOperationAction(ISD::CTPOP, MVT::i16, Expand);

  // We have SWPB for byte swap
  setOperationAction(ISD::BSWAP, MVT::i16, Legal);

  // Rotate - expand to shifts + or. SRC instruction only supports constant
  // count; variable-count rotate needs expansion.
  setOperationAction(ISD::ROTR, MVT::i16, Expand);
  setOperationAction(ISD::ROTL, MVT::i16, Expand);

  // Shifts are supported
  setOperationAction(ISD::SHL, MVT::i16, Legal);
  setOperationAction(ISD::SRA, MVT::i16, Legal);
  setOperationAction(ISD::SRL, MVT::i16, Legal);

  // Sign extend and zero extend
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i8, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  // We don't have conditional moves
  setOperationAction(ISD::SELECT, MVT::i16, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i16, Custom);

  // Branch handling
  setOperationAction(ISD::BR_CC, MVT::i16, Custom);
  setOperationAction(ISD::BR_CC, MVT::i32, Custom);
  setOperationAction(ISD::BRCOND, MVT::Other, Custom);

  // Switch/jump table support
  // BR_JT is expanded into: load target address from table, branch indirect
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  // JumpTable needs custom lowering to wrap with TMS9900ISD::Wrapper
  setOperationAction(ISD::JumpTable, MVT::i16, Custom);
  // BlockAddress for computed goto support
  setOperationAction(ISD::BlockAddress, MVT::i16, Custom);

  // Global addresses
  setOperationAction(ISD::GlobalAddress, MVT::i16, Custom);

  // Frame and return address
  setOperationAction(ISD::FRAMEADDR, MVT::i16, Custom);
  setOperationAction(ISD::RETURNADDR, MVT::i16, Custom);

  // Dynamic stack allocation not supported
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i16, Expand);
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);

  // Varargs support
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAARG, MVT::Other, Expand);   // Use default expansion
  setOperationAction(ISD::VACOPY, MVT::Other, Expand);  // Just copies pointer
  setOperationAction(ISD::VAEND, MVT::Other, Expand);   // No-op

  // ===== 32-bit (i32) Operations =====
  // TMS9900 is a 16-bit CPU. We expand 32-bit operations into 16-bit
  // operations where possible, allowing the optimizer to work on them.
  // Division/remainder still use libcalls due to complexity.

  // Arithmetic - expand to pairs of 16-bit ops
  // Note: ADD/SUB expansion requires carry handling which we synthesize
  setOperationAction(ISD::ADD, MVT::i32, Expand);
  setOperationAction(ISD::SUB, MVT::i32, Expand);
  setOperationAction(ISD::MUL, MVT::i32, Expand);    // Expands to UMUL_LOHI or partial products

  // Division/remainder - keep as libcalls (too complex to inline efficiently)
  setOperationAction(ISD::SDIV, MVT::i32, LibCall);  // __divsi3
  setOperationAction(ISD::UDIV, MVT::i32, LibCall);  // __udivsi3
  setOperationAction(ISD::SREM, MVT::i32, LibCall);  // __modsi3
  setOperationAction(ISD::UREM, MVT::i32, LibCall);  // __umodsi3
  setOperationAction(ISD::SDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i32, Expand);

  // 32-bit shifts - custom lower (inline constant shifts, libcall for variable)
  // __ashlsi3 (shift left), __lshrsi3 (logical right), __ashrsi3 (arithmetic right)
  setOperationAction(ISD::SHL, MVT::i32, Custom);
  setOperationAction(ISD::SRA, MVT::i32, Custom);
  setOperationAction(ISD::SRL, MVT::i32, Custom);

  // Multi-word shift parts - custom handling for libcalls
  setOperationAction(ISD::SHL_PARTS, MVT::i16, Custom);
  setOperationAction(ISD::SRA_PARTS, MVT::i16, Custom);
  setOperationAction(ISD::SRL_PARTS, MVT::i16, Custom);

  // Bit counting - expand
  setOperationAction(ISD::CTTZ, MVT::i32, Expand);
  setOperationAction(ISD::CTLZ, MVT::i32, Expand);
  setOperationAction(ISD::CTPOP, MVT::i32, Expand);

  // Byte swap - expand (would need custom sequence)
  setOperationAction(ISD::BSWAP, MVT::i32, Expand);

  // Rotates - expand
  setOperationAction(ISD::ROTL, MVT::i32, Expand);
  setOperationAction(ISD::ROTR, MVT::i32, Expand);

  // Overflow operations - expand
  setOperationAction(ISD::SMUL_LOHI, MVT::i32, Expand);
  setOperationAction(ISD::UMUL_LOHI, MVT::i32, Expand);
  setOperationAction(ISD::MULHS, MVT::i32, Expand);
  setOperationAction(ISD::MULHU, MVT::i32, Expand);

  // Carry operations - we don't have ADC/SBC instructions
  setOperationAction(ISD::ADDC, MVT::i16, Expand);
  setOperationAction(ISD::SUBC, MVT::i16, Expand);
  setOperationAction(ISD::ADDE, MVT::i16, Expand);
  setOperationAction(ISD::SUBE, MVT::i16, Expand);
  setOperationAction(ISD::UADDO, MVT::i16, Expand);
  setOperationAction(ISD::USUBO, MVT::i16, Expand);
  setOperationAction(ISD::SADDO, MVT::i16, Expand);
  setOperationAction(ISD::SSUBO, MVT::i16, Expand);

  // Select operations for i32
  setOperationAction(ISD::SELECT, MVT::i32, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Expand);

  // SETCC for i16 - custom to keep compare/branch adjacent (flags are fragile)
  setOperationAction(ISD::SETCC, MVT::i16, Custom);
  setOperationAction(ISD::SETCC, MVT::i32, Custom);

  // Set minimum function alignment
  setMinFunctionAlignment(Align(2));
  setPrefFunctionAlignment(Align(2));
}

void TMS9900TargetLowering::computeKnownBitsForFrameIndex(
    int FIOp, KnownBits &Known, const MachineFunction &MF) const {
  // Do NOT report any known bits for frame indices.
  //
  // The default implementation reports low bits as known-zero based on
  // the object's requested alignment (e.g., Align(4) for i32 means bits
  // 0-1 are known zero). The DAG combiner uses this to convert
  // ADD(frameptr, 2) -> OR(frameptr, 2) when it thinks bit 1 is zero.
  //
  // On TMS9900, the actual address of a frame object depends on the
  // stack layout after prologue emission (DECT for R11 push + alignment
  // padding). The requested alignment may not reflect the actual runtime
  // alignment of the object's address. Reporting false known-zero bits
  // causes OR to be used instead of ADD, which is a no-op when the bit
  // is actually set, silently reading the wrong memory location.
  //
  // Being conservative here costs negligible optimization opportunity
  // but prevents a class of silent miscompilation bugs.
  Known.resetAll();
}

const char *TMS9900TargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch ((TMS9900ISD::NodeType)Opcode) {
  case TMS9900ISD::FIRST_NUMBER:
    break;
  case TMS9900ISD::RET:
    return "TMS9900ISD::RET";
  case TMS9900ISD::RETI:
    return "TMS9900ISD::RETI";
  case TMS9900ISD::CALL:
    return "TMS9900ISD::CALL";
  case TMS9900ISD::Wrapper:
    return "TMS9900ISD::Wrapper";
  case TMS9900ISD::CMP:
    return "TMS9900ISD::CMP";
  case TMS9900ISD::SELECT_CC:
    return "TMS9900ISD::SELECT_CC";
  case TMS9900ISD::BR_CC:
    return "TMS9900ISD::BR_CC";
  case TMS9900ISD::MPY:
    return "TMS9900ISD::MPY";
  case TMS9900ISD::BYTE_STORE:
    return "TMS9900ISD::BYTE_STORE";
  case TMS9900ISD::BYTE_LOAD:
    return "TMS9900ISD::BYTE_LOAD";
  case TMS9900ISD::TAIL_CALL:
    return "TMS9900ISD::TAIL_CALL";
  }
  return nullptr;
}

EVT TMS9900TargetLowering::getSetCCResultType(const DataLayout &DL,
                                               LLVMContext &Context,
                                               EVT VT) const {
  return MVT::i16;
}

//===----------------------------------------------------------------------===//
// Inline Assembly Support
//===----------------------------------------------------------------------===//

/// getConstraintType - Given a constraint letter, return the type of
/// constraint it is for this target.
TargetLowering::ConstraintType
TMS9900TargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default:
      break;
    case 'r': // General purpose register
      return C_RegisterClass;
    case 'i': // Immediate integer
    case 'n': // Immediate integer with known value
      return C_Immediate;
    case 'm': // Memory operand
      return C_Memory;
    }
  }

  // Check for specific register names like {R0}, {R1}, etc.
  if (Constraint.size() > 1 && Constraint[0] == '{' &&
      Constraint.back() == '}') {
    return C_Register;
  }

  return TargetLowering::getConstraintType(Constraint);
}

/// getRegForInlineAsmConstraint - Given a constraint and operand type,
/// return the register class and register to use.
std::pair<unsigned, const TargetRegisterClass *>
TMS9900TargetLowering::getRegForInlineAsmConstraint(
    const TargetRegisterInfo *TRI, StringRef Constraint, MVT VT) const {

  if (Constraint.size() == 1) {
    // GCC-style constraint letters
    switch (Constraint[0]) {
    default:
      break;
    case 'r': // General purpose register
      return std::make_pair(0U, &TMS9900::GR16RegClass);
    }
  }

  // Handle explicit register names: {R0}, {R1}, etc.
  if (Constraint.size() > 2 && Constraint[0] == '{' &&
      Constraint.back() == '}') {
    StringRef RegName = Constraint.slice(1, Constraint.size() - 1);

    // Map register names to registers
    if (RegName == "R0" || RegName == "r0")
      return std::make_pair(TMS9900::R0, &TMS9900::GR16RegClass);
    if (RegName == "R1" || RegName == "r1")
      return std::make_pair(TMS9900::R1, &TMS9900::GR16RegClass);
    if (RegName == "R2" || RegName == "r2")
      return std::make_pair(TMS9900::R2, &TMS9900::GR16RegClass);
    if (RegName == "R3" || RegName == "r3")
      return std::make_pair(TMS9900::R3, &TMS9900::GR16RegClass);
    if (RegName == "R4" || RegName == "r4")
      return std::make_pair(TMS9900::R4, &TMS9900::GR16RegClass);
    if (RegName == "R5" || RegName == "r5")
      return std::make_pair(TMS9900::R5, &TMS9900::GR16RegClass);
    if (RegName == "R6" || RegName == "r6")
      return std::make_pair(TMS9900::R6, &TMS9900::GR16RegClass);
    if (RegName == "R7" || RegName == "r7")
      return std::make_pair(TMS9900::R7, &TMS9900::GR16RegClass);
    if (RegName == "R8" || RegName == "r8")
      return std::make_pair(TMS9900::R8, &TMS9900::GR16RegClass);
    if (RegName == "R9" || RegName == "r9")
      return std::make_pair(TMS9900::R9, &TMS9900::GR16RegClass);
    if (RegName == "R10" || RegName == "r10")
      return std::make_pair(TMS9900::R10, &TMS9900::GR16RegClass);
    if (RegName == "R11" || RegName == "r11")
      return std::make_pair(TMS9900::R11, &TMS9900::GR16RegClass);
    if (RegName == "R12" || RegName == "r12")
      return std::make_pair(TMS9900::R12, &TMS9900::GR16RegClass);
    if (RegName == "R13" || RegName == "r13")
      return std::make_pair(TMS9900::R13, &TMS9900::GR16RegClass);
    if (RegName == "R14" || RegName == "r14")
      return std::make_pair(TMS9900::R14, &TMS9900::GR16RegClass);
    if (RegName == "R15" || RegName == "r15")
      return std::make_pair(TMS9900::R15, &TMS9900::GR16RegClass);
  }

  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

SDValue TMS9900TargetLowering::LowerOperation(SDValue Op,
                                               SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("unimplemented operation");
  case ISD::STORE:
    return LowerSTORE(Op, DAG);
  case ISD::LOAD:
    return LowerLOAD(Op, DAG);
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::JumpTable:
    return LowerJumpTable(Op, DAG);
  case ISD::BlockAddress:
    return LowerBlockAddress(Op, DAG);
  case ISD::BR_CC:
    return LowerBR_CC(Op, DAG);
  case ISD::BRCOND:
    return LowerBRCOND(Op, DAG);
  case ISD::SETCC:
    return LowerSETCC(Op, DAG);
  case ISD::SELECT_CC:
    return LowerSELECT_CC(Op, DAG);
  case ISD::SHL_PARTS:
    return LowerShiftParts(Op, DAG, true, false);
  case ISD::SRA_PARTS:
    return LowerShiftParts(Op, DAG, false, true);
  case ISD::SRL_PARTS:
    return LowerShiftParts(Op, DAG, false, false);
  case ISD::SHL:
  case ISD::SRA:
  case ISD::SRL:
    return LowerShift32(Op, DAG);
  case ISD::VASTART:
    return LowerVASTART(Op, DAG);
  case ISD::RETURNADDR:
    return LowerRETURNADDR(Op, DAG);
  case ISD::FRAMEADDR:
    return LowerFRAMEADDR(Op, DAG);
  }
}

void TMS9900TargetLowering::ReplaceNodeResults(SDNode *N,
                                                SmallVectorImpl<SDValue> &Results,
                                                SelectionDAG &DAG) const {
  SDLoc DL(N);
  switch (N->getOpcode()) {
  default:
    return; // Leave Results empty to fall through to default expansion
  case ISD::SHL:
  case ISD::SRA:
  case ISD::SRL: {
    // During type legalization, i32 shifts need custom expansion.
    // Delegate to LowerShift32 which correctly splits into i16 operations.
    SDValue Result = LowerShift32(SDValue(N, 0), DAG);
    if (Result.getNode())
      Results.push_back(Result);
    return;
  }
  }
}

SDValue TMS9900TargetLowering::LowerLOAD(SDValue Op, SelectionDAG &DAG) const {
  LoadSDNode *LD = cast<LoadSDNode>(Op);
  SDLoc DL(Op);

  // Don't handle indexed loads here - they're handled in instruction selection
  if (LD->getAddressingMode() != ISD::UNINDEXED)
    return SDValue();

  // Handle i8 loads - TMS9900 MOVB loads into HIGH byte of register
  if (LD->getMemoryVT() == MVT::i8) {
    SDValue Chain = LD->getChain();
    SDValue Ptr = LD->getBasePtr();
    ISD::LoadExtType ExtType = LD->getExtensionType();

    // Create our custom BYTE_LOAD node - will be matched to MOVB
    // MOVB loads byte into HIGH byte position
    SDValue ByteLoad = DAG.getNode(TMS9900ISD::BYTE_LOAD, DL,
                                   DAG.getVTList(MVT::i16, MVT::Other),
                                   Chain, Ptr);

    // Shift right by 8 to move byte from HIGH to LOW position
    SDValue ShiftAmt = DAG.getConstant(8, DL, MVT::i16);
    SDValue ShiftedVal;
    if (ExtType == ISD::SEXTLOAD) {
      // Arithmetic shift for sign extension
      ShiftedVal = DAG.getNode(ISD::SRA, DL, MVT::i16, ByteLoad, ShiftAmt);
    } else {
      // Logical shift for zero extension (also for EXTLOAD and NON_EXTLOAD)
      ShiftedVal = DAG.getNode(ISD::SRL, DL, MVT::i16, ByteLoad, ShiftAmt);
    }

    // Return both the shifted value and the chain
    SDValue Results[] = {ShiftedVal, ByteLoad.getValue(1)};
    return DAG.getMergeValues(Results, DL);
  }

  // Other loads are handled by default patterns
  return SDValue();
}

SDValue TMS9900TargetLowering::LowerSTORE(SDValue Op, SelectionDAG &DAG) const {
  StoreSDNode *ST = cast<StoreSDNode>(Op);
  SDLoc DL(Op);

  // Don't handle indexed stores here - they're handled in instruction selection
  if (ST->getAddressingMode() != ISD::UNINDEXED)
    return SDValue();

  // Handle i8 stores - TMS9900 MOVB uses HIGH byte of register
  if (ST->getMemoryVT() == MVT::i8) {
    SDValue Chain = ST->getChain();
    SDValue Value = ST->getValue();
    SDValue Ptr = ST->getBasePtr();

    if (Value.getValueType() == MVT::i16) {
      // Shift value left by 8 to put the byte in HIGH position for MOVB
      SDValue ShiftAmt = DAG.getConstant(8, DL, MVT::i16);
      SDValue ShiftedVal = DAG.getNode(ISD::SHL, DL, MVT::i16, Value, ShiftAmt);

      // Create our custom BYTE_STORE node - will be matched to MOVB
      return DAG.getNode(TMS9900ISD::BYTE_STORE, DL, MVT::Other,
                         Chain, ShiftedVal, Ptr);
    }
  }

  // Other stores are handled by default patterns
  return SDValue();
}

bool TMS9900TargetLowering::getPostIndexedAddressParts(
    SDNode *N, SDNode *Op, SDValue &Base, SDValue &Offset,
    ISD::MemIndexedMode &AM, SelectionDAG &DAG) const {
  // Check if this is a load or store that we can convert to post-increment
  EVT VT;
  SDValue Ptr;

  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    VT = LD->getMemoryVT();
    Ptr = LD->getBasePtr();
  } else if (StoreSDNode *ST = dyn_cast<StoreSDNode>(N)) {
    VT = ST->getMemoryVT();
    Ptr = ST->getBasePtr();
  } else {
    return false;
  }

  // Only support i16 and i8
  if (VT != MVT::i16 && VT != MVT::i8)
    return false;

  // Check if Op is an add that can be folded into post-increment
  if (Op->getOpcode() != ISD::ADD)
    return false;

  // One operand should be the pointer, the other should be a constant
  SDValue AddOp0 = Op->getOperand(0);
  SDValue AddOp1 = Op->getOperand(1);

  // Check if this add is Ptr + Const
  if (AddOp0 != Ptr)
    std::swap(AddOp0, AddOp1);
  if (AddOp0 != Ptr)
    return false;

  // Check if the offset is the right increment value
  // Word operations increment by 2, byte operations increment by 1
  ConstantSDNode *COffset = dyn_cast<ConstantSDNode>(AddOp1);
  if (!COffset)
    return false;

  int64_t OffsetVal = COffset->getSExtValue();
  int64_t ExpectedOffset = (VT == MVT::i16) ? 2 : 1;

  if (OffsetVal != ExpectedOffset)
    return false;

  // This can be a post-increment operation
  Base = Ptr;
  Offset = AddOp1;
  AM = ISD::POST_INC;
  return true;
}

SDValue TMS9900TargetLowering::LowerGlobalAddress(SDValue Op,
                                                   SelectionDAG &DAG) const {
  SDLoc DL(Op);
  const GlobalValue *GV = cast<GlobalAddressSDNode>(Op)->getGlobal();
  int64_t Offset = cast<GlobalAddressSDNode>(Op)->getOffset();

  SDValue Result = DAG.getTargetGlobalAddress(GV, DL, MVT::i16, Offset);
  return DAG.getNode(TMS9900ISD::Wrapper, DL, MVT::i16, Result);
}

SDValue TMS9900TargetLowering::LowerJumpTable(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDLoc DL(Op);
  JumpTableSDNode *JT = cast<JumpTableSDNode>(Op);
  EVT PtrVT = Op.getValueType();
  SDValue Result = DAG.getTargetJumpTable(JT->getIndex(), PtrVT);
  return DAG.getNode(TMS9900ISD::Wrapper, DL, PtrVT, Result);
}

SDValue TMS9900TargetLowering::LowerBlockAddress(SDValue Op,
                                                  SelectionDAG &DAG) const {
  SDLoc DL(Op);
  const BlockAddress *BA = cast<BlockAddressSDNode>(Op)->getBlockAddress();
  EVT PtrVT = Op.getValueType();
  SDValue Result = DAG.getTargetBlockAddress(BA, PtrVT);
  return DAG.getNode(TMS9900ISD::Wrapper, DL, PtrVT, Result);
}

/// LowerShiftParts - Lower SHL_PARTS/SRA_PARTS/SRL_PARTS for 32-bit shifts
/// These are generated when a 32-bit value needs to be shifted.
///
/// SHL_PARTS: (Lo, Hi, Amt) -> (ResLo, ResHi)
/// SRL_PARTS: (Lo, Hi, Amt) -> (ResLo, ResHi)
/// SRA_PARTS: (Lo, Hi, Amt) -> (ResLo, ResHi)
///
/// For TMS9900, we generate a sequence that handles variable shift amounts.
/// For constant shifts, the optimizer may simplify this.
SDValue TMS9900TargetLowering::LowerShiftParts(SDValue Op, SelectionDAG &DAG,
                                                bool IsLeft, bool IsArithmetic) const {
  SDLoc DL(Op);
  SDValue Lo = Op.getOperand(0);
  SDValue Hi = Op.getOperand(1);
  SDValue Amt = Op.getOperand(2);
  EVT VT = Lo.getValueType();

  // For now, we generate a libcall for variable shifts to keep code simple.
  // The libcall approach is still available in libtms9900.

  // Check if shift amount is a constant
  if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(Amt)) {
    unsigned ShiftAmt = CN->getZExtValue();

    // Shift by 0 - no-op
    if (ShiftAmt == 0) {
      return DAG.getMergeValues({Lo, Hi}, DL);
    }

    // Shift by >= 32 - result depends on operation type
    if (ShiftAmt >= 32) {
      if (IsLeft) {
        // Left shift by >= 32: result is 0
        SDValue Zero = DAG.getConstant(0, DL, VT);
        return DAG.getMergeValues({Zero, Zero}, DL);
      } else if (IsArithmetic) {
        // Arithmetic right shift by >= 32: result is sign extension
        SDValue SignBit = DAG.getNode(ISD::SRA, DL, VT, Hi,
                                       DAG.getConstant(15, DL, VT));
        return DAG.getMergeValues({SignBit, SignBit}, DL);
      } else {
        // Logical right shift by >= 32: result is 0
        SDValue Zero = DAG.getConstant(0, DL, VT);
        return DAG.getMergeValues({Zero, Zero}, DL);
      }
    }

    // Shift by 16 - special case, just swap words
    if (ShiftAmt == 16) {
      if (IsLeft) {
        SDValue Zero = DAG.getConstant(0, DL, VT);
        return DAG.getMergeValues({Zero, Lo}, DL);
      } else {
        SDValue Fill = IsArithmetic ?
          DAG.getNode(ISD::SRA, DL, VT, Hi, DAG.getConstant(15, DL, VT)) :
          DAG.getConstant(0, DL, VT);
        return DAG.getMergeValues({Hi, Fill}, DL);
      }
    }

    // Shift by > 16 but < 32
    if (ShiftAmt > 16) {
      unsigned SmallShift = ShiftAmt - 16;
      if (IsLeft) {
        SDValue Zero = DAG.getConstant(0, DL, VT);
        SDValue NewHi = DAG.getNode(ISD::SHL, DL, VT, Lo,
                                     DAG.getConstant(SmallShift, DL, VT));
        return DAG.getMergeValues({Zero, NewHi}, DL);
      } else {
        SDValue NewLo = IsArithmetic ?
          DAG.getNode(ISD::SRA, DL, VT, Hi, DAG.getConstant(SmallShift, DL, VT)) :
          DAG.getNode(ISD::SRL, DL, VT, Hi, DAG.getConstant(SmallShift, DL, VT));
        SDValue Fill = IsArithmetic ?
          DAG.getNode(ISD::SRA, DL, VT, Hi, DAG.getConstant(15, DL, VT)) :
          DAG.getConstant(0, DL, VT);
        return DAG.getMergeValues({NewLo, Fill}, DL);
      }
    }

    // Shift by < 16 - need to combine bits from both words
    SDValue ShiftAmtVal = DAG.getConstant(ShiftAmt, DL, VT);
    SDValue InvShiftAmt = DAG.getConstant(16 - ShiftAmt, DL, VT);

    if (IsLeft) {
      // NewHi = (Hi << Amt) | (Lo >> (16 - Amt))
      // NewLo = Lo << Amt
      SDValue HiShifted = DAG.getNode(ISD::SHL, DL, VT, Hi, ShiftAmtVal);
      SDValue LoBits = DAG.getNode(ISD::SRL, DL, VT, Lo, InvShiftAmt);
      SDValue NewHi = DAG.getNode(ISD::OR, DL, VT, HiShifted, LoBits);
      SDValue NewLo = DAG.getNode(ISD::SHL, DL, VT, Lo, ShiftAmtVal);
      return DAG.getMergeValues({NewLo, NewHi}, DL);
    } else {
      // NewLo = (Lo >> Amt) | (Hi << (16 - Amt))
      // NewHi = Hi >> Amt (arithmetic or logical)
      SDValue LoShifted = DAG.getNode(ISD::SRL, DL, VT, Lo, ShiftAmtVal);
      SDValue HiBits = DAG.getNode(ISD::SHL, DL, VT, Hi, InvShiftAmt);
      SDValue NewLo = DAG.getNode(ISD::OR, DL, VT, LoShifted, HiBits);
      SDValue NewHi = IsArithmetic ?
        DAG.getNode(ISD::SRA, DL, VT, Hi, ShiftAmtVal) :
        DAG.getNode(ISD::SRL, DL, VT, Hi, ShiftAmtVal);
      return DAG.getMergeValues({NewLo, NewHi}, DL);
    }
  }

  // Variable shift amount - use libcall
  // Combine lo/hi into i32, call libcall, split result back

  // Build the i32 value from lo and hi parts
  SDValue Val = DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i32, Lo, Hi);

  // Determine which libcall to use
  RTLIB::Libcall LC;
  if (IsLeft) {
    LC = RTLIB::SHL_I32;
  } else if (IsArithmetic) {
    LC = RTLIB::SRA_I32;
  } else {
    LC = RTLIB::SRL_I32;
  }

  // Make the libcall
  TargetLowering::MakeLibCallOptions CallOptions;
  SDValue Result = makeLibCall(DAG, LC, MVT::i32, {Val, Amt}, CallOptions, DL).first;

  // Extract lo and hi from the result
  SDValue ResLo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, VT, Result,
                               DAG.getConstant(0, DL, MVT::i16));
  SDValue ResHi = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, VT, Result,
                               DAG.getConstant(1, DL, MVT::i16));

  return DAG.getMergeValues({ResLo, ResHi}, DL);
}

/// LowerShift32 - Lower 32-bit shifts to libcalls
/// __ashlsi3 (shift left), __lshrsi3 (logical right), __ashrsi3 (arithmetic right)
SDValue TMS9900TargetLowering::LowerShift32(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();

  // Only handle i32 shifts
  if (VT != MVT::i32)
    return SDValue();

  SDValue Val = Op.getOperand(0);
  SDValue Amt = Op.getOperand(1);

  if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(Amt)) {
    unsigned ShiftAmt = CN->getZExtValue();

    if (ShiftAmt == 0)
      return Val;

    SDValue Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i16, Val,
                             DAG.getConstant(0, DL, MVT::i16));
    SDValue Hi = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i16, Val,
                             DAG.getConstant(1, DL, MVT::i16));

    SDValue ResLo;
    SDValue ResHi;
    EVT HalfVT = MVT::i16;

    if (ShiftAmt >= 32) {
      if (Op.getOpcode() == ISD::SHL || Op.getOpcode() == ISD::SRL) {
        ResLo = DAG.getConstant(0, DL, HalfVT);
        ResHi = DAG.getConstant(0, DL, HalfVT);
      } else {
        SDValue Sign = DAG.getNode(ISD::SRA, DL, HalfVT, Hi,
                                   DAG.getConstant(15, DL, HalfVT));
        ResLo = Sign;
        ResHi = Sign;
      }
      return DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i32, ResLo, ResHi);
    }

    if (ShiftAmt == 16) {
      if (Op.getOpcode() == ISD::SHL) {
        ResLo = DAG.getConstant(0, DL, HalfVT);
        ResHi = Lo;
      } else {
        ResLo = Hi;
        if (Op.getOpcode() == ISD::SRA) {
          ResHi = DAG.getNode(ISD::SRA, DL, HalfVT, Hi,
                              DAG.getConstant(15, DL, HalfVT));
        } else {
          ResHi = DAG.getConstant(0, DL, HalfVT);
        }
      }
      return DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i32, ResLo, ResHi);
    }

    if (ShiftAmt > 16) {
      unsigned SmallShift = ShiftAmt - 16;
      SDValue SmallAmt = DAG.getConstant(SmallShift, DL, HalfVT);
      if (Op.getOpcode() == ISD::SHL) {
        ResLo = DAG.getConstant(0, DL, HalfVT);
        ResHi = DAG.getNode(ISD::SHL, DL, HalfVT, Lo, SmallAmt);
      } else if (Op.getOpcode() == ISD::SRA) {
        ResLo = DAG.getNode(ISD::SRA, DL, HalfVT, Hi, SmallAmt);
        ResHi = DAG.getNode(ISD::SRA, DL, HalfVT, Hi,
                            DAG.getConstant(15, DL, HalfVT));
      } else {
        ResLo = DAG.getNode(ISD::SRL, DL, HalfVT, Hi, SmallAmt);
        ResHi = DAG.getConstant(0, DL, HalfVT);
      }
      return DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i32, ResLo, ResHi);
    }

    SDValue ShiftAmtVal = DAG.getConstant(ShiftAmt, DL, HalfVT);
    SDValue InvShiftAmt = DAG.getConstant(16 - ShiftAmt, DL, HalfVT);

    if (Op.getOpcode() == ISD::SHL) {
      SDValue HiShifted = DAG.getNode(ISD::SHL, DL, HalfVT, Hi, ShiftAmtVal);
      SDValue LoBits = DAG.getNode(ISD::SRL, DL, HalfVT, Lo, InvShiftAmt);
      ResHi = DAG.getNode(ISD::OR, DL, HalfVT, HiShifted, LoBits);
      ResLo = DAG.getNode(ISD::SHL, DL, HalfVT, Lo, ShiftAmtVal);
    } else {
      SDValue LoShifted = DAG.getNode(ISD::SRL, DL, HalfVT, Lo, ShiftAmtVal);
      SDValue HiBits = DAG.getNode(ISD::SHL, DL, HalfVT, Hi, InvShiftAmt);
      ResLo = DAG.getNode(ISD::OR, DL, HalfVT, LoShifted, HiBits);
      if (Op.getOpcode() == ISD::SRA) {
        ResHi = DAG.getNode(ISD::SRA, DL, HalfVT, Hi, ShiftAmtVal);
      } else {
        ResHi = DAG.getNode(ISD::SRL, DL, HalfVT, Hi, ShiftAmtVal);
      }
    }

    return DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i32, ResLo, ResHi);
  }

  // Determine which libcall to use
  RTLIB::Libcall LC;
  switch (Op.getOpcode()) {
  default: llvm_unreachable("Invalid shift opcode");
  case ISD::SHL: LC = RTLIB::SHL_I32; break;
  case ISD::SRL: LC = RTLIB::SRL_I32; break;
  case ISD::SRA: LC = RTLIB::SRA_I32; break;
  }

  // The shift amount needs to be i16 for the libcall
  if (Amt.getValueType() != MVT::i16)
    Amt = DAG.getZExtOrTrunc(Amt, DL, MVT::i16);

  // Make the libcall
  TargetLowering::MakeLibCallOptions CallOptions;
  return makeLibCall(DAG, LC, VT, {Val, Amt}, CallOptions, DL).first;
}

SDValue TMS9900TargetLowering::LowerVASTART(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  TMS9900MachineFunctionInfo *FuncInfo = MF.getInfo<TMS9900MachineFunctionInfo>();
  SDLoc DL(Op);

  // vastart stores the address of the first vararg to the va_list pointer
  SDValue Ptr = Op.getOperand(1);  // va_list pointer
  EVT PtrVT = Ptr.getValueType();

  // Get the frame index of the first vararg
  SDValue FrameIndex = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), PtrVT);

  // Get the source value for the store
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();

  // Store the frame index to the va_list pointer
  return DAG.getStore(Op.getOperand(0), DL, FrameIndex, Ptr,
                      MachinePointerInfo(SV));
}

SDValue TMS9900TargetLowering::LowerRETURNADDR(SDValue Op,
                                                SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setReturnAddressIsTaken(true);

  if (verifyReturnAddressArgumentIsConstant(Op, DAG))
    return SDValue();

  EVT VT = Op.getValueType();
  SDLoc DL(Op);
  unsigned Depth = Op.getConstantOperandVal(0);

  if (Depth > 0) {
    // We don't have frame pointers to walk the call stack.
    // Return null for depth > 0.
    return DAG.getConstant(0, DL, VT);
  }

  // Depth 0: return the current return address from R11 (link register).
  // Mark R11 as a live-in so the register allocator knows it's used.
  Register Reg = MF.addLiveIn(TMS9900::R11, &TMS9900::GR16RegClass);
  return DAG.getCopyFromReg(DAG.getEntryNode(), DL, Reg, VT);
}

SDValue TMS9900TargetLowering::LowerFRAMEADDR(SDValue Op,
                                               SelectionDAG &DAG) const {
  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  MFI.setFrameAddressIsTaken(true);

  EVT VT = Op.getValueType();
  SDLoc DL(Op);
  unsigned Depth = Op.getConstantOperandVal(0);

  if (Depth > 0) {
    // No frame pointer chain to walk. Return null.
    return DAG.getConstant(0, DL, VT);
  }

  // Depth 0: return the stack pointer (R10). TMS9900 does not use a
  // dedicated frame pointer, so SP is the best approximation.
  return DAG.getCopyFromReg(DAG.getEntryNode(), DL, TMS9900::R10, VT);
}

SDValue TMS9900TargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc DL(Op);

  if (LHS.getValueType() == MVT::i32) {
    SDValue Cond = DAG.getSetCC(DL, MVT::i16, LHS, RHS, CC);
    return DAG.getNode(ISD::BRCOND, DL, MVT::Other, Chain, Cond, Dest);
  }

  // Normalize operand order so the compare computes (LHS - RHS).
  // TMS9900 C/CI set flags based on (dest - src). For register compares,
  // that means dest is the second operand (C src,dst). For immediate compares,
  // dest is the register operand (CI reg,imm). We order operands to keep the
  // comparison semantics consistent across reg/reg and reg/imm cases.
  auto isImm = [](SDValue V) {
    return isa<ConstantSDNode>(V);
  };

  // If the LHS is immediate, swap operands and condition to keep dest in a reg.
  if (isImm(LHS) && !isImm(RHS)) {
    std::swap(LHS, RHS);
    CC = ISD::getSetCCSwappedOperands(CC);
  }

  SDValue CmpLHS;
  SDValue CmpRHS;
  if (isImm(RHS)) {
    // CI: dest is the register operand (LHS).
    CmpLHS = LHS;
    CmpRHS = RHS;
  } else {
    // C: dest is the second operand, so swap to compare (LHS - RHS).
    CmpLHS = RHS;
    CmpRHS = LHS;
  }

  // Create comparison
  SDValue Cmp = DAG.getNode(TMS9900ISD::CMP, DL, MVT::Glue, CmpLHS, CmpRHS);

  // Create branch with condition
  return DAG.getNode(TMS9900ISD::BR_CC, DL, MVT::Other, Chain, Dest,
                     DAG.getConstant(CC, DL, MVT::i16), Cmp);
}

SDValue TMS9900TargetLowering::LowerBRCOND(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  SDValue Cond = Op.getOperand(1);
  SDValue Dest = Op.getOperand(2);
  SDLoc DL(Op);

  ISD::CondCode CC = ISD::SETNE;
  SDValue LHS;
  SDValue RHS;

  if (Cond.getOpcode() == ISD::SETCC) {
    LHS = Cond.getOperand(0);
    RHS = Cond.getOperand(1);
    CC = cast<CondCodeSDNode>(Cond.getOperand(2))->get();
    if (LHS.getValueType() != MVT::i16) {
      // Let non-i16 SETCC lower normally; branch on the computed boolean.
      LHS = SDValue();
      RHS = SDValue();
    }
  } else {
    if (Cond.getValueType() != MVT::i16) {
      Cond = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i16, Cond);
    }
    LHS = Cond;
    RHS = DAG.getConstant(0, DL, MVT::i16);
  }

  if (!LHS.getNode()) {
    if (Cond.getValueType() != MVT::i16) {
      Cond = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i16, Cond);
    }
    LHS = Cond;
    RHS = DAG.getConstant(0, DL, MVT::i16);
    CC = ISD::SETNE;
  }

  auto isImm = [](SDValue V) {
    return isa<ConstantSDNode>(V);
  };

  if (isImm(LHS) && !isImm(RHS)) {
    std::swap(LHS, RHS);
    CC = ISD::getSetCCSwappedOperands(CC);
  }

  SDValue CmpLHS;
  SDValue CmpRHS;
  if (isImm(RHS)) {
    CmpLHS = LHS;
    CmpRHS = RHS;
  } else {
    CmpLHS = RHS;
    CmpRHS = LHS;
  }

  SDValue Cmp = DAG.getNode(TMS9900ISD::CMP, DL, MVT::Glue, CmpLHS, CmpRHS);

  return DAG.getNode(TMS9900ISD::BR_CC, DL, MVT::Other, Chain, Dest,
                     DAG.getConstant(CC, DL, MVT::i16), Cmp);
}

SDValue TMS9900TargetLowering::LowerSETCC(SDValue Op,
                                          SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(2))->get();
  SDLoc DL(Op);

  if (LHS.getValueType() == MVT::i16) {
    SDValue TrueVal = DAG.getAllOnesConstant(DL, MVT::i16);
    SDValue FalseVal = DAG.getConstant(0, DL, MVT::i16);
    SDValue Sel = DAG.getSelectCC(DL, LHS, RHS, TrueVal, FalseVal, CC);
    return Sel;
  }

  if (LHS.getValueType() != MVT::i32)
    return SDValue();

  // Split i32 operands into hi/lo i16 halves using EXTRACT_ELEMENT.
  // This avoids creating i32 SRL nodes which would trigger type legalization
  // issues (ReplaceNodeResults needed for Custom-action i32 shifts).
  SDValue LHSLo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i16, LHS,
                              DAG.getConstant(0, DL, MVT::i16));
  SDValue LHSHi = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i16, LHS,
                              DAG.getConstant(1, DL, MVT::i16));
  SDValue RHSLo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i16, RHS,
                              DAG.getConstant(0, DL, MVT::i16));
  SDValue RHSHi = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i16, RHS,
                              DAG.getConstant(1, DL, MVT::i16));

  auto setcc16 = [&](SDValue A, SDValue B, ISD::CondCode Cc) {
    return DAG.getSetCC(DL, MVT::i16, A, B, Cc);
  };
  auto And = [&](SDValue A, SDValue B) {
    return DAG.getNode(ISD::AND, DL, MVT::i16, A, B);
  };
  auto Or = [&](SDValue A, SDValue B) {
    return DAG.getNode(ISD::OR, DL, MVT::i16, A, B);
  };

  SDValue HiEq = setcc16(LHSHi, RHSHi, ISD::SETEQ);
  SDValue HiNe = setcc16(LHSHi, RHSHi, ISD::SETNE);
  SDValue LoEq = setcc16(LHSLo, RHSLo, ISD::SETEQ);
  SDValue LoNe = setcc16(LHSLo, RHSLo, ISD::SETNE);

  SDValue HiLtS = setcc16(LHSHi, RHSHi, ISD::SETLT);
  SDValue HiGtS = setcc16(LHSHi, RHSHi, ISD::SETGT);
  SDValue HiLtU = setcc16(LHSHi, RHSHi, ISD::SETULT);
  SDValue HiGtU = setcc16(LHSHi, RHSHi, ISD::SETUGT);

  SDValue LoLtU = setcc16(LHSLo, RHSLo, ISD::SETULT);
  SDValue LoGtU = setcc16(LHSLo, RHSLo, ISD::SETUGT);
  SDValue LoLeU = setcc16(LHSLo, RHSLo, ISD::SETULE);
  SDValue LoGeU = setcc16(LHSLo, RHSLo, ISD::SETUGE);

  switch (CC) {
  case ISD::SETEQ:
    return And(HiEq, LoEq);
  case ISD::SETNE:
    return Or(HiNe, LoNe);
  case ISD::SETLT:
    return Or(HiLtS, And(HiEq, LoLtU));
  case ISD::SETGT:
    return Or(HiGtS, And(HiEq, LoGtU));
  case ISD::SETLE:
    return Or(HiLtS, And(HiEq, LoLeU));
  case ISD::SETGE:
    return Or(HiGtS, And(HiEq, LoGeU));
  case ISD::SETULT:
    return Or(HiLtU, And(HiEq, LoLtU));
  case ISD::SETUGT:
    return Or(HiGtU, And(HiEq, LoGtU));
  case ISD::SETULE:
    return Or(HiLtU, And(HiEq, LoLeU));
  case ISD::SETUGE:
    return Or(HiGtU, And(HiEq, LoGeU));
  default:
    llvm_unreachable("Unsupported SETCC for i32");
  }
}

SDValue TMS9900TargetLowering::LowerSELECT_CC(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDValue TrueVal = Op.getOperand(2);
  SDValue FalseVal = Op.getOperand(3);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDLoc DL(Op);

  // Create SELECT_CC with 5 operands: trueval, falseval, cc, lhs, rhs.
  // The compare+branch lives in the SELECT16 custom inserter (CMPBR),
  // so don't emit a separate compare here.
  SDValue Ops[] = {TrueVal, FalseVal, DAG.getConstant(CC, DL, MVT::i16),
                   LHS, RHS};
  return DAG.getNode(TMS9900ISD::SELECT_CC, DL, Op.getValueType(), Ops);
}

MachineBasicBlock *
TMS9900TargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                    MachineBasicBlock *BB) const {
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("Unexpected instruction for custom inserter");

  case TMS9900::MUL16: {
    // MUL16 $dst, $lhs, $rhs
    // Expands to:
    //   MOV $lhs, R0    ; Put first operand in R0 (even register)
    //   MPY $rhs        ; R0:R1 = R0 * $rhs (uses R0 implicitly)
    //   MOV R1, $dst    ; Result (low 16 bits) is in R1
    Register DstReg = MI.getOperand(0).getReg();
    Register LhsReg = MI.getOperand(1).getReg();
    Register RhsReg = MI.getOperand(2).getReg();

    // MOV $lhs, R0
    BuildMI(*BB, MI, DL, TII.get(TMS9900::MOVrr), TMS9900::R0)
        .addReg(LhsReg);

    // MPY $rhs,R0 (multiplicand in R0, result in R0:R1)
    BuildMI(*BB, MI, DL, TII.get(TMS9900::MPYrr))
        .addReg(RhsReg)
        .addReg(TMS9900::R0);

    // MOV R1, $dst (low 16 bits of result are in R1)
    BuildMI(*BB, MI, DL, TII.get(TMS9900::MOVrr), DstReg)
        .addReg(TMS9900::R1);

    MI.eraseFromParent();
    return BB;
  }

  case TMS9900::UDIV16: {
    // UDIV16 $dst, $dividend, $divisor
    // Expands to:
    //   CLR R0          ; High word of 32-bit dividend = 0
    //   MOV $dividend, R1  ; Low word of dividend
    //   DIV $divisor, R0   ; R0 = quotient, R1 = remainder
    //   MOV R0, $dst       ; Quotient is the result
    Register DstReg = MI.getOperand(0).getReg();
    Register DividendReg = MI.getOperand(1).getReg();
    Register DivisorReg = MI.getOperand(2).getReg();

    // CLR R0 (dividend high = 0)
    BuildMI(*BB, MI, DL, TII.get(TMS9900::CLRr), TMS9900::R0);

    // MOV $dividend, R1
    BuildMI(*BB, MI, DL, TII.get(TMS9900::MOVrr), TMS9900::R1)
        .addReg(DividendReg);

    // DIV $divisor,R0 (divides R0:R1, quotient in R0, remainder in R1)
    BuildMI(*BB, MI, DL, TII.get(TMS9900::DIVrr))
        .addReg(DivisorReg)
        .addReg(TMS9900::R0);

    // MOV R0, $dst (quotient)
    BuildMI(*BB, MI, DL, TII.get(TMS9900::MOVrr), DstReg)
        .addReg(TMS9900::R0);

    MI.eraseFromParent();
    return BB;
  }

  case TMS9900::UREM16: {
    // UREM16 $dst, $dividend, $divisor
    // Same as UDIV16 but result comes from R1 (remainder)
    Register DstReg = MI.getOperand(0).getReg();
    Register DividendReg = MI.getOperand(1).getReg();
    Register DivisorReg = MI.getOperand(2).getReg();

    // CLR R0 (dividend high = 0)
    BuildMI(*BB, MI, DL, TII.get(TMS9900::CLRr), TMS9900::R0);

    // MOV $dividend, R1
    BuildMI(*BB, MI, DL, TII.get(TMS9900::MOVrr), TMS9900::R1)
        .addReg(DividendReg);

    // DIV $divisor,R0 (divides R0:R1, quotient in R0, remainder in R1)
    BuildMI(*BB, MI, DL, TII.get(TMS9900::DIVrr))
        .addReg(DivisorReg)
        .addReg(TMS9900::R0);

    // MOV R1, $dst (remainder)
    BuildMI(*BB, MI, DL, TII.get(TMS9900::MOVrr), DstReg)
        .addReg(TMS9900::R1);

    MI.eraseFromParent();
    return BB;
  }

  case TMS9900::SDIV16: {
    // SDIV16 $dst, $dividend, $divisor
    // TMS9900 DIV is unsigned. For signed divide:
    // 1. Compute sign of result (XOR of operand signs)
    // 2. Take absolute values
    // 3. Unsigned divide
    // 4. Negate quotient if signs differed
    //
    // We use R2 for sign tracking and R3 as scratch for abs values
    // R0:R1 are used for the division itself
    Register DstReg = MI.getOperand(0).getReg();
    Register DividendReg = MI.getOperand(1).getReg();
    Register DivisorReg = MI.getOperand(2).getReg();

    MachineFunction *MF = BB->getParent();

    // Create basic blocks for the control flow
    MachineBasicBlock *StartBB = BB;
    MachineBasicBlock *DividendNegBB = MF->CreateMachineBasicBlock();
    MachineBasicBlock *CheckDivisorBB = MF->CreateMachineBasicBlock();
    MachineBasicBlock *DivisorNegBB = MF->CreateMachineBasicBlock();
    MachineBasicBlock *DoDivideBB = MF->CreateMachineBasicBlock();
    MachineBasicBlock *NegateResultBB = MF->CreateMachineBasicBlock();
    MachineBasicBlock *DoneBB = MF->CreateMachineBasicBlock();

    // Insert all new blocks
    MachineFunction::iterator It = ++BB->getIterator();
    MF->insert(It, DividendNegBB);
    MF->insert(It, CheckDivisorBB);
    MF->insert(It, DivisorNegBB);
    MF->insert(It, DoDivideBB);
    MF->insert(It, NegateResultBB);
    MF->insert(It, DoneBB);

    // Transfer successors to DoneBB
    DoneBB->splice(DoneBB->begin(), StartBB,
                   std::next(MachineBasicBlock::iterator(MI)), StartBB->end());
    DoneBB->transferSuccessorsAndUpdatePHIs(StartBB);

    // Add live-ins to ensure registers are preserved across blocks
    DividendNegBB->addLiveIn(TMS9900::R1);
    DividendNegBB->addLiveIn(TMS9900::R2);
    DividendNegBB->addLiveIn(TMS9900::R3);
    CheckDivisorBB->addLiveIn(TMS9900::R1);
    CheckDivisorBB->addLiveIn(TMS9900::R2);
    CheckDivisorBB->addLiveIn(TMS9900::R3);
    DivisorNegBB->addLiveIn(TMS9900::R1);
    DivisorNegBB->addLiveIn(TMS9900::R2);
    DivisorNegBB->addLiveIn(TMS9900::R3);
    DoDivideBB->addLiveIn(TMS9900::R1);
    DoDivideBB->addLiveIn(TMS9900::R2);
    DoDivideBB->addLiveIn(TMS9900::R3);
    NegateResultBB->addLiveIn(TMS9900::R0);
    DoneBB->addLiveIn(TMS9900::R0);

    // StartBB: Initialize sign tracker, check dividend sign
    // CLR R2 (sign tracker = 0)
    BuildMI(StartBB, DL, TII.get(TMS9900::CLRr), TMS9900::R2);

    // MOV $dividend, R1 (put dividend in R1)
    BuildMI(StartBB, DL, TII.get(TMS9900::MOVrr), TMS9900::R1)
        .addReg(DividendReg);

    // MOV $divisor, R3 (put divisor in R3 for abs value)
    BuildMI(StartBB, DL, TII.get(TMS9900::MOVrr), TMS9900::R3)
        .addReg(DivisorReg);

    // Test R1 (dividend) - JLT if negative (signed compare with 0)
    BuildMI(StartBB, DL, TII.get(TMS9900::CI))
        .addReg(TMS9900::R1)
        .addImm(0);
    BuildMI(StartBB, DL, TII.get(TMS9900::JLT))
        .addMBB(DividendNegBB);
    BuildMI(StartBB, DL, TII.get(TMS9900::JMP))
        .addMBB(CheckDivisorBB);
    StartBB->addSuccessor(DividendNegBB);
    StartBB->addSuccessor(CheckDivisorBB);

    // DividendNegBB: Dividend is negative, negate it and flip sign
    BuildMI(DividendNegBB, DL, TII.get(TMS9900::NEGr), TMS9900::R1)
        .addReg(TMS9900::R1);
    // INV R2 to flip sign tracker (0 -> -1, -1 -> 0)
    BuildMI(DividendNegBB, DL, TII.get(TMS9900::INVr), TMS9900::R2)
        .addReg(TMS9900::R2);
    BuildMI(DividendNegBB, DL, TII.get(TMS9900::JMP))
        .addMBB(CheckDivisorBB);
    DividendNegBB->addSuccessor(CheckDivisorBB);

    // CheckDivisorBB: Check divisor sign
    BuildMI(CheckDivisorBB, DL, TII.get(TMS9900::CI))
        .addReg(TMS9900::R3)
        .addImm(0);
    BuildMI(CheckDivisorBB, DL, TII.get(TMS9900::JLT))
        .addMBB(DivisorNegBB);
    BuildMI(CheckDivisorBB, DL, TII.get(TMS9900::JMP))
        .addMBB(DoDivideBB);
    CheckDivisorBB->addSuccessor(DivisorNegBB);
    CheckDivisorBB->addSuccessor(DoDivideBB);

    // DivisorNegBB: Divisor is negative, negate it and flip sign
    BuildMI(DivisorNegBB, DL, TII.get(TMS9900::NEGr), TMS9900::R3)
        .addReg(TMS9900::R3);
    BuildMI(DivisorNegBB, DL, TII.get(TMS9900::INVr), TMS9900::R2)
        .addReg(TMS9900::R2);
    BuildMI(DivisorNegBB, DL, TII.get(TMS9900::JMP))
        .addMBB(DoDivideBB);
    DivisorNegBB->addSuccessor(DoDivideBB);

    // DoDivideBB: Perform unsigned divide with absolute values
    // CLR R0 (high word = 0)
    BuildMI(DoDivideBB, DL, TII.get(TMS9900::CLRr), TMS9900::R0);
    // DIV R3,R0 (divides R0:R1 by R3, quotient in R0, remainder in R1)
    BuildMI(DoDivideBB, DL, TII.get(TMS9900::DIVrr))
        .addReg(TMS9900::R3)
        .addReg(TMS9900::R0);
    // Check if we need to negate result (R2 != 0)
    BuildMI(DoDivideBB, DL, TII.get(TMS9900::CI))
        .addReg(TMS9900::R2)
        .addImm(0);
    BuildMI(DoDivideBB, DL, TII.get(TMS9900::JNE))
        .addMBB(NegateResultBB);
    BuildMI(DoDivideBB, DL, TII.get(TMS9900::JMP))
        .addMBB(DoneBB);
    DoDivideBB->addSuccessor(NegateResultBB);
    DoDivideBB->addSuccessor(DoneBB);

    // NegateResultBB: Negate the quotient
    BuildMI(NegateResultBB, DL, TII.get(TMS9900::NEGr), TMS9900::R0)
        .addReg(TMS9900::R0);
    BuildMI(NegateResultBB, DL, TII.get(TMS9900::JMP))
        .addMBB(DoneBB);
    NegateResultBB->addSuccessor(DoneBB);

    // DoneBB: Copy result to destination
    BuildMI(*DoneBB, DoneBB->begin(), DL, TII.get(TMS9900::MOVrr), DstReg)
        .addReg(TMS9900::R0);

    MI.eraseFromParent();
    return DoneBB;
  }

  case TMS9900::SREM16: {
    // SREM16 $dst, $dividend, $divisor
    // Similar to SDIV16 but:
    // - Result is the remainder (from R1 after DIV)
    // - Sign of remainder follows sign of dividend (C99 semantics)
    Register DstReg = MI.getOperand(0).getReg();
    Register DividendReg = MI.getOperand(1).getReg();
    Register DivisorReg = MI.getOperand(2).getReg();

    MachineFunction *MF = BB->getParent();

    // Create basic blocks for the control flow
    MachineBasicBlock *StartBB = BB;
    MachineBasicBlock *DividendNegBB = MF->CreateMachineBasicBlock();
    MachineBasicBlock *CheckDivisorBB = MF->CreateMachineBasicBlock();
    MachineBasicBlock *DivisorNegBB = MF->CreateMachineBasicBlock();
    MachineBasicBlock *DoDivideBB = MF->CreateMachineBasicBlock();
    MachineBasicBlock *NegateResultBB = MF->CreateMachineBasicBlock();
    MachineBasicBlock *DoneBB = MF->CreateMachineBasicBlock();

    // Insert all new blocks
    MachineFunction::iterator It = ++BB->getIterator();
    MF->insert(It, DividendNegBB);
    MF->insert(It, CheckDivisorBB);
    MF->insert(It, DivisorNegBB);
    MF->insert(It, DoDivideBB);
    MF->insert(It, NegateResultBB);
    MF->insert(It, DoneBB);

    // Transfer successors to DoneBB
    DoneBB->splice(DoneBB->begin(), StartBB,
                   std::next(MachineBasicBlock::iterator(MI)), StartBB->end());
    DoneBB->transferSuccessorsAndUpdatePHIs(StartBB);

    // Add live-ins to ensure registers are preserved across blocks
    DividendNegBB->addLiveIn(TMS9900::R1);
    DividendNegBB->addLiveIn(TMS9900::R3);
    CheckDivisorBB->addLiveIn(TMS9900::R1);
    CheckDivisorBB->addLiveIn(TMS9900::R2);
    CheckDivisorBB->addLiveIn(TMS9900::R3);
    DivisorNegBB->addLiveIn(TMS9900::R1);
    DivisorNegBB->addLiveIn(TMS9900::R2);
    DivisorNegBB->addLiveIn(TMS9900::R3);
    DoDivideBB->addLiveIn(TMS9900::R1);
    DoDivideBB->addLiveIn(TMS9900::R2);
    DoDivideBB->addLiveIn(TMS9900::R3);
    NegateResultBB->addLiveIn(TMS9900::R1);
    DoneBB->addLiveIn(TMS9900::R1);

    // StartBB: Initialize sign tracker (for dividend only), check dividend sign
    // CLR R2 (dividend sign tracker = 0 means positive)
    BuildMI(StartBB, DL, TII.get(TMS9900::CLRr), TMS9900::R2);

    // MOV $dividend, R1 (put dividend in R1)
    BuildMI(StartBB, DL, TII.get(TMS9900::MOVrr), TMS9900::R1)
        .addReg(DividendReg);

    // MOV $divisor, R3 (put divisor in R3 for abs value)
    BuildMI(StartBB, DL, TII.get(TMS9900::MOVrr), TMS9900::R3)
        .addReg(DivisorReg);

    // Test R1 (dividend) - JLT if negative
    BuildMI(StartBB, DL, TII.get(TMS9900::CI))
        .addReg(TMS9900::R1)
        .addImm(0);
    BuildMI(StartBB, DL, TII.get(TMS9900::JLT))
        .addMBB(DividendNegBB);
    BuildMI(StartBB, DL, TII.get(TMS9900::JMP))
        .addMBB(CheckDivisorBB);
    StartBB->addSuccessor(DividendNegBB);
    StartBB->addSuccessor(CheckDivisorBB);

    // DividendNegBB: Dividend is negative, negate it and mark
    BuildMI(DividendNegBB, DL, TII.get(TMS9900::NEGr), TMS9900::R1)
        .addReg(TMS9900::R1);
    // Set R2 to non-zero to indicate dividend was negative
    BuildMI(DividendNegBB, DL, TII.get(TMS9900::SETOr), TMS9900::R2);
    BuildMI(DividendNegBB, DL, TII.get(TMS9900::JMP))
        .addMBB(CheckDivisorBB);
    DividendNegBB->addSuccessor(CheckDivisorBB);

    // CheckDivisorBB: Check divisor sign (only need abs value, not tracking)
    BuildMI(CheckDivisorBB, DL, TII.get(TMS9900::CI))
        .addReg(TMS9900::R3)
        .addImm(0);
    BuildMI(CheckDivisorBB, DL, TII.get(TMS9900::JLT))
        .addMBB(DivisorNegBB);
    BuildMI(CheckDivisorBB, DL, TII.get(TMS9900::JMP))
        .addMBB(DoDivideBB);
    CheckDivisorBB->addSuccessor(DivisorNegBB);
    CheckDivisorBB->addSuccessor(DoDivideBB);

    // DivisorNegBB: Divisor is negative, negate it (no sign tracking needed)
    BuildMI(DivisorNegBB, DL, TII.get(TMS9900::NEGr), TMS9900::R3)
        .addReg(TMS9900::R3);
    BuildMI(DivisorNegBB, DL, TII.get(TMS9900::JMP))
        .addMBB(DoDivideBB);
    DivisorNegBB->addSuccessor(DoDivideBB);

    // DoDivideBB: Perform unsigned divide
    // CLR R0 (high word = 0)
    BuildMI(DoDivideBB, DL, TII.get(TMS9900::CLRr), TMS9900::R0);
    // DIV R3,R0 (divides R0:R1 by R3, quotient in R0, remainder in R1)
    BuildMI(DoDivideBB, DL, TII.get(TMS9900::DIVrr))
        .addReg(TMS9900::R3)
        .addReg(TMS9900::R0);
    // Check if we need to negate remainder (dividend was negative)
    BuildMI(DoDivideBB, DL, TII.get(TMS9900::CI))
        .addReg(TMS9900::R2)
        .addImm(0);
    BuildMI(DoDivideBB, DL, TII.get(TMS9900::JNE))
        .addMBB(NegateResultBB);
    BuildMI(DoDivideBB, DL, TII.get(TMS9900::JMP))
        .addMBB(DoneBB);
    DoDivideBB->addSuccessor(NegateResultBB);
    DoDivideBB->addSuccessor(DoneBB);

    // NegateResultBB: Negate the remainder
    BuildMI(NegateResultBB, DL, TII.get(TMS9900::NEGr), TMS9900::R1)
        .addReg(TMS9900::R1);
    BuildMI(NegateResultBB, DL, TII.get(TMS9900::JMP))
        .addMBB(DoneBB);
    NegateResultBB->addSuccessor(DoneBB);

    // DoneBB: Copy result to destination
    BuildMI(*DoneBB, DoneBB->begin(), DL, TII.get(TMS9900::MOVrr), DstReg)
        .addReg(TMS9900::R1);  // Remainder is in R1

    MI.eraseFromParent();
    return DoneBB;
  }

  case TMS9900::SELECT16: {
    // SELECT16 $dst, $trueval, $falseval, $cc, $lhs, $rhs
    // Implements: dst = (lhs cc rhs) ? trueval : falseval
    //
    // Expansion:
    //   C $lhs, $rhs (or CI if immediate)
    //   J<cc> TrueBB
    //   JMP FalseBB
    // TrueBB:
    //   MOV $trueval, $dst
    //   JMP DoneBB
    // FalseBB:
    //   MOV $falseval, $dst
    // DoneBB:
    //   (continue)

    Register DstReg = MI.getOperand(0).getReg();
    Register TrueReg = MI.getOperand(1).getReg();
    Register FalseReg = MI.getOperand(2).getReg();
    int64_t CCVal = MI.getOperand(3).getImm();
    Register LHSReg = MI.getOperand(4).getReg();
    Register RHSReg = MI.getOperand(5).getReg();

    MachineFunction *MF = BB->getParent();
    MachineRegisterInfo &MRI = MF->getRegInfo();
    assert(Register::isVirtualRegister(DstReg) &&
           "SELECT16 expects a virtual destination register");
    const TargetRegisterClass *RC = MRI.getRegClass(DstReg);
    Register TrueVReg = MRI.createVirtualRegister(RC);
    Register FalseVReg = MRI.createVirtualRegister(RC);

    MachineBasicBlock *StartBB = BB;
    MachineBasicBlock *TrueBB = MF->CreateMachineBasicBlock();
    MachineBasicBlock *FalseBB = MF->CreateMachineBasicBlock();
    MachineBasicBlock *DoneBB = MF->CreateMachineBasicBlock();

    MachineFunction::iterator It = ++BB->getIterator();
    MF->insert(It, TrueBB);
    MF->insert(It, FalseBB);
    MF->insert(It, DoneBB);

    // Transfer everything after this instruction to DoneBB
    DoneBB->splice(DoneBB->begin(), StartBB,
                   std::next(MachineBasicBlock::iterator(MI)), StartBB->end());
    DoneBB->transferSuccessorsAndUpdatePHIs(StartBB);

    // Note: Don't add virtual registers as live-ins - that's for physical regs
    // The register allocator will handle virtual register liveness

    // Emit a CMPBR pseudo so the compare/branch stay adjacent (flags are
    // otherwise clobbered by intervening instructions).
    auto emitCmpBr = [&](MachineBasicBlock *TargetBB, ISD::CondCode Cond) {
      // CMPBR expands to: C RHS, LHS (flags reflect LHS - RHS), then Jcc.
      BuildMI(StartBB, DL, TII.get(TMS9900::CMPBRrr))
          .addReg(RHSReg)
          .addReg(LHSReg)
          .addImm(static_cast<int64_t>(Cond))
          .addMBB(TargetBB);
    };

    // Choose the right conditional jump based on CC
    ISD::CondCode CC = static_cast<ISD::CondCode>(CCVal);
    switch (CC) {
    case ISD::SETEQ:
    case ISD::SETNE:
    case ISD::SETGT:
    case ISD::SETLT:
    case ISD::SETUGT:
    case ISD::SETULT:
    case ISD::SETUGE:
    case ISD::SETULE:
      emitCmpBr(TrueBB, CC);
      BuildMI(StartBB, DL, TII.get(TMS9900::JMP)).addMBB(FalseBB);
      StartBB->addSuccessor(TrueBB);
      StartBB->addSuccessor(FalseBB);
      goto skip_normal_jump;
    case ISD::SETGE:
      // >= is tricky, need JGT or JEQ. Use JLT to FalseBB instead.
      emitCmpBr(FalseBB, ISD::SETLT);
      BuildMI(StartBB, DL, TII.get(TMS9900::JMP)).addMBB(TrueBB);
      StartBB->addSuccessor(FalseBB);
      StartBB->addSuccessor(TrueBB);
      goto skip_normal_jump;
    case ISD::SETLE:
      // <= is JLT or JEQ. Use JGT to FalseBB instead.
      emitCmpBr(FalseBB, ISD::SETGT);
      BuildMI(StartBB, DL, TII.get(TMS9900::JMP)).addMBB(TrueBB);
      StartBB->addSuccessor(FalseBB);
      StartBB->addSuccessor(TrueBB);
      goto skip_normal_jump;
    default:
      llvm_unreachable("Unsupported condition code in SELECT16");
    }

skip_normal_jump:
    // TrueBB: Copy true value to a temporary
    BuildMI(TrueBB, DL, TII.get(TMS9900::MOVrr), TrueVReg)
        .addReg(TrueReg);
    BuildMI(TrueBB, DL, TII.get(TMS9900::JMP)).addMBB(DoneBB);
    TrueBB->addSuccessor(DoneBB);

    // FalseBB: Copy false value to a temporary
    BuildMI(FalseBB, DL, TII.get(TMS9900::MOVrr), FalseVReg)
        .addReg(FalseReg);
    // Fall through to DoneBB (or add explicit jump)
    FalseBB->addSuccessor(DoneBB);

    // DoneBB: Merge the select result.
    BuildMI(*DoneBB, DoneBB->begin(), DL, TII.get(TargetOpcode::PHI), DstReg)
        .addReg(TrueVReg)
        .addMBB(TrueBB)
        .addReg(FalseVReg)
        .addMBB(FalseBB);
    MF->getProperties().reset(MachineFunctionProperties::Property::NoPHIs);

    MI.eraseFromParent();
    return DoneBB;
  }

  case TMS9900::SLA_VAR:
  case TMS9900::SRA_VAR:
  case TMS9900::SRL_VAR: {
    // Variable shift: MOV $cnt, R0 + shift $rs, 0
    // TMS9900 hardware quirk: when count field is 0 (meaning "use R0"),
    // and R0 is also 0, the shift count becomes 16 (not 0).
    // We must guard against this by skipping the shift when count=0.
    //
    // Key: use CMPBRri (a terminator pseudo) for the zero check.
    // PHI elimination inserts copies BEFORE terminators, so the
    // CMPBRri→(CI+JEQ) expansion in expandPostRAPseudo stays atomic.
    // The MOV $cnt, R0 is placed in ShiftBB where it can't be disrupted.
    //
    // Expansion:
    //   StartBB: CMPBRri $cnt, 0, SETEQ, DoneBB   (skip if count=0)
    //   ShiftBB: MOV $cnt, R0; SLA/SRA/SRL $rs, 0  (shift by R0)
    //   DoneBB:  PHI($dst, shifted from ShiftBB, original from StartBB)

    Register DstReg = MI.getOperand(0).getReg();
    Register SrcReg = MI.getOperand(1).getReg();
    Register CntReg = MI.getOperand(2).getReg();

    unsigned ShiftOpc;
    switch (MI.getOpcode()) {
    case TMS9900::SLA_VAR: ShiftOpc = TMS9900::SLAr0; break;
    case TMS9900::SRA_VAR: ShiftOpc = TMS9900::SRAr0; break;
    case TMS9900::SRL_VAR: ShiftOpc = TMS9900::SRLr0; break;
    default: llvm_unreachable("unexpected opcode");
    }

    MachineFunction *MF = BB->getParent();
    MachineRegisterInfo &MRI = MF->getRegInfo();
    const TargetRegisterClass *RC = MRI.getRegClass(DstReg);

    MachineBasicBlock *StartBB = BB;
    MachineBasicBlock *ShiftBB = MF->CreateMachineBasicBlock();
    MachineBasicBlock *DoneBB = MF->CreateMachineBasicBlock();

    MachineFunction::iterator It = ++BB->getIterator();
    MF->insert(It, ShiftBB);
    MF->insert(It, DoneBB);

    // Transfer everything after this instruction to DoneBB
    DoneBB->splice(DoneBB->begin(), StartBB,
                   std::next(MachineBasicBlock::iterator(MI)), StartBB->end());
    DoneBB->transferSuccessorsAndUpdatePHIs(StartBB);

    // StartBB: CMPBRri $cnt, 0, SETEQ, DoneBB (skip shift if count=0)
    // CMPBRri is a terminator — PHI copies go before it, not between CI and JEQ.
    BuildMI(StartBB, DL, TII.get(TMS9900::CMPBRri))
        .addReg(CntReg)
        .addImm(0)
        .addImm(ISD::SETEQ)
        .addMBB(DoneBB);
    StartBB->addSuccessor(ShiftBB);
    StartBB->addSuccessor(DoneBB);

    // ShiftBB: MOV $cnt, R0 then shift, fall through to DoneBB
    Register ShiftedReg = MRI.createVirtualRegister(RC);
    BuildMI(ShiftBB, DL, TII.get(TMS9900::MOVrr), TMS9900::R0)
        .addReg(CntReg);
    BuildMI(ShiftBB, DL, TII.get(ShiftOpc), ShiftedReg)
        .addReg(SrcReg);
    ShiftBB->addSuccessor(DoneBB);

    // DoneBB: PHI to pick shifted or original value
    BuildMI(*DoneBB, DoneBB->begin(), DL, TII.get(TargetOpcode::PHI), DstReg)
        .addReg(ShiftedReg)
        .addMBB(ShiftBB)
        .addReg(SrcReg)
        .addMBB(StartBB);
    MF->getProperties().reset(MachineFunctionProperties::Property::NoPHIs);

    MI.eraseFromParent();
    return DoneBB;
  }
  }

  return BB;
}

//===----------------------------------------------------------------------===//
//             Formal Arguments Calling Convention Implementation
//===----------------------------------------------------------------------===//

SDValue TMS9900TargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();
  TMS9900MachineFunctionInfo *FuncInfo = MF.getInfo<TMS9900MachineFunctionInfo>();

  // Analyze arguments
  // For vararg functions, use stack-only calling convention so that
  // va_arg can walk arguments sequentially on the stack.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, MF, ArgLocs, *DAG.getContext());
  if (isVarArg)
    CCInfo.AnalyzeFormalArguments(Ins, CC_TMS9900_VarArg);
  else
    CCInfo.AnalyzeFormalArguments(Ins, CC_TMS9900);

  // For vararg functions, create a frame index for the start of varargs area
  // This is the first stack location after all named arguments
  if (isVarArg) {
    // The offset is at the end of where all named arguments go on the stack
    // CCInfo.getStackSize() gives us the stack usage for all named arguments
    unsigned Offset = CCInfo.getStackSize();
    int VarArgsFrameIndex = MFI.CreateFixedObject(2, Offset, true);
    FuncInfo->setVarArgsFrameIndex(VarArgsFrameIndex);
  }

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];

    if (VA.isRegLoc()) {
      // Argument passed in a register
      EVT RegVT = VA.getLocVT();
      const TargetRegisterClass *RC = &TMS9900::GR16RegClass;

      Register VReg = RegInfo.createVirtualRegister(RC);
      RegInfo.addLiveIn(VA.getLocReg(), VReg);
      SDValue ArgValue = DAG.getCopyFromReg(Chain, DL, VReg, RegVT);

      InVals.push_back(ArgValue);
    } else {
      // Argument passed on stack
      assert(VA.isMemLoc());

      // Create a fixed stack object for this incoming argument
      // Stack arguments are above the return address and saved frame
      // The offset from CCState is relative to the incoming stack pointer
      unsigned ObjSize = VA.getLocVT().getSizeInBits() / 8;
      int FI = MFI.CreateFixedObject(ObjSize, VA.getLocMemOffset(), true);

      // Create a load from the stack slot
      SDValue FIN = DAG.getFrameIndex(FI, MVT::i16);
      SDValue ArgValue = DAG.getLoad(
          VA.getLocVT(), DL, Chain, FIN,
          MachinePointerInfo::getFixedStack(MF, FI));

      InVals.push_back(ArgValue);
    }
  }

  return Chain;
}

//===----------------------------------------------------------------------===//
//                  Return Value Calling Convention Implementation
//===----------------------------------------------------------------------===//

bool TMS9900TargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool isVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context) const {
  // TMS9900 can return up to 8 bytes (4 x i16 registers: R0-R3).
  // Anything larger (e.g. i128) must use sret (struct return).
  unsigned TotalBytes = 0;
  for (const auto &Out : Outs)
    TotalBytes += Out.VT.getSizeInBits() / 8;
  return TotalBytes <= 8;
}

SDValue TMS9900TargetLowering::LowerReturn(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
    SelectionDAG &DAG) const {

  MachineFunction &MF = DAG.getMachineFunction();
  const Function &F = MF.getFunction();

  // Naked functions don't emit any return instruction - the user is
  // responsible for the entire function body including return
  if (F.hasFnAttribute(Attribute::Naked)) {
    return Chain;
  }

  // Interrupt handlers use RTWP instead of B *R11
  bool IsInterrupt = F.hasFnAttribute("interrupt");

  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, MF, RVLocs, *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_TMS9900);

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  // Copy return values to their registers (skip for interrupt handlers
  // since they don't return values in the traditional sense)
  if (!IsInterrupt) {
    for (unsigned i = 0, e = RVLocs.size(); i != e; ++i) {
      CCValAssign &VA = RVLocs[i];
      assert(VA.isRegLoc() && "Return values must be in registers");

      Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), OutVals[i], Glue);
      Glue = Chain.getValue(1);
      RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
    }
  }

  RetOps[0] = Chain;

  if (Glue.getNode())
    RetOps.push_back(Glue);

  // Use RETI (RTWP) for interrupt handlers, RET (B *R11) for normal functions
  unsigned RetOpc = IsInterrupt ? TMS9900ISD::RETI : TMS9900ISD::RET;
  return DAG.getNode(RetOpc, DL, MVT::Other, RetOps);
}

//===----------------------------------------------------------------------===//
//                        Tail Call Optimization
//===----------------------------------------------------------------------===//

bool TMS9900TargetLowering::mayBeEmittedAsTailCall(const CallInst *CI) const {
  return CI->isTailCall();
}

/// isEligibleForTailCallOptimization - Check whether the call is eligible
/// for tail call optimization.
///
/// Criteria for TMS9900 tail call optimization:
/// - No stack arguments (all args must fit in R0-R3)
/// - No byval arguments
/// - No struct return (sret)
/// - Caller is not an interrupt handler
/// - Caller is not a varargs function
/// - Calling conventions must be compatible
bool TMS9900TargetLowering::isEligibleForTailCallOptimization(
    CCState &CCInfo, CallLoweringInfo &CLI, MachineFunction &MF,
    const SmallVector<CCValAssign, 16> &ArgLocs) const {

  auto &Caller = MF.getFunction();

  // Interrupt handlers cannot use tail calls (they use RTWP to return)
  if (Caller.hasFnAttribute("interrupt"))
    return false;

  // Naked functions cannot use tail calls (no prologue/epilogue)
  if (Caller.hasFnAttribute(Attribute::Naked))
    return false;

  // Do not tail call if the callee requires stack arguments.
  // TMS9900 passes first 4 args in R0-R3; if we need more, stack is required.
  if (CCInfo.getStackSize() != 0)
    return false;

  // Do not tail call if caller is a varargs function.
  // Varargs functions have complex stack layouts we don't want to disturb.
  if (CLI.IsVarArg)
    return false;

  // Do not tail call opt if either caller or callee uses struct return.
  auto IsCallerStructRet = Caller.hasStructRetAttr();
  auto IsCalleeStructRet = CLI.Outs.empty() ? false : CLI.Outs[0].Flags.isSRet();
  if (IsCallerStructRet || IsCalleeStructRet)
    return false;

  // Check that no arguments are passed by value.
  for (auto &Arg : CLI.Outs)
    if (Arg.Flags.isByVal())
      return false;

  // The callee must use the same calling convention as the caller,
  // or at minimum preserve the same registers.
  auto CallerCC = Caller.getCallingConv();
  auto CalleeCC = CLI.CallConv;
  if (CallerCC != CalleeCC) {
    const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();
    const uint32_t *CallerPreserved = TRI->getCallPreservedMask(MF, CallerCC);
    const uint32_t *CalleePreserved = TRI->getCallPreservedMask(MF, CalleeCC);
    if (!TRI->regmaskSubsetEqual(CallerPreserved, CalleePreserved))
      return false;
  }

  return true;
}

//===----------------------------------------------------------------------===//
//                           Call Lowering
//===----------------------------------------------------------------------===//

SDValue TMS9900TargetLowering::LowerCall(
    TargetLowering::CallLoweringInfo &CLI,
    SmallVectorImpl<SDValue> &InVals) const {

  SelectionDAG &DAG = CLI.DAG;
  SDLoc &DL = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  bool &IsTailCall = CLI.IsTailCall;
  CallingConv::ID CallConv = CLI.CallConv;
  bool isVarArg = CLI.IsVarArg;

  MachineFunction &MF = DAG.getMachineFunction();

  // Analyze operands of the call, assigning locations to each operand.
  // For vararg calls, use stack-only calling convention so callee can
  // walk arguments sequentially via va_arg.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, MF, ArgLocs, *DAG.getContext());
  if (isVarArg)
    CCInfo.AnalyzeCallOperands(Outs, CC_TMS9900_VarArg);
  else
    CCInfo.AnalyzeCallOperands(Outs, CC_TMS9900);

  // Check if tail call optimization is possible.
  if (IsTailCall)
    IsTailCall = isEligibleForTailCallOptimization(CCInfo, CLI, MF, ArgLocs);

  if (IsTailCall && CLI.CB && CLI.CB->isMustTailCall())
    if (!IsTailCall)
      report_fatal_error("failed to perform tail call elimination on a call "
                         "site marked musttail");

  // Get the size of the outgoing arguments area
  unsigned NumBytes = CCInfo.getStackSize();

  // For non-tail calls, emit CALLSEQ_START to mark the beginning of the
  // call sequence. Tail calls don't need this since they reuse the caller's
  // stack frame.
  if (!IsTailCall)
    Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, DL);

  // Handle byval arguments: for each byval parameter, create a local stack
  // copy and replace the original pointer with the copy's address.
  // This must be done before the argument-passing loop because the CC has
  // already assigned the byval pointer to a register or stack slot, and we
  // need the pointer to point to the copy, not the original.
  for (unsigned i = 0, e = Outs.size(); i != e; ++i) {
    ISD::ArgFlagsTy Flags = Outs[i].Flags;
    if (!Flags.isByVal())
      continue;

    SDValue Src = OutVals[i]; // pointer to original struct
    unsigned Size = Flags.getByValSize();
    Align Alignment = Flags.getNonZeroByValAlign();

    // Create a stack object for the byval copy.
    int FI = MF.getFrameInfo().CreateStackObject(Size, Alignment, false);
    SDValue Dst = DAG.getFrameIndex(FI, MVT::i16);

    // Emit memcpy from original to stack copy.
    SDValue SizeNode = DAG.getConstant(Size, DL, MVT::i16);
    Chain = DAG.getMemcpy(Chain, DL, Dst, Src, SizeNode, Alignment,
                          /*isVolatile=*/false,
                          /*AlwaysInline=*/true,
                          /*isTailCall=*/false,
                          MachinePointerInfo::getFixedStack(MF, FI),
                          MachinePointerInfo());

    // Replace the argument with the pointer to the copy.
    OutVals[i] = Dst;
  }

  // Build a sequence of copy-to-reg nodes chained together
  SDValue InGlue;
  SmallVector<std::pair<unsigned, SDValue>, 4> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;
  SDValue StackPtr;

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    SDValue Arg = OutVals[i];

    if (VA.isRegLoc()) {
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
    } else {
      // Stack argument
      assert(VA.isMemLoc());
      assert(!IsTailCall && "Tail call not allowed if stack is used "
                            "for passing parameters");

      // Get the stack pointer if we haven't already
      if (!StackPtr.getNode())
        StackPtr = DAG.getCopyFromReg(Chain, DL, TMS9900::R10, MVT::i16);

      // Calculate the address for this argument on the stack
      // VA.getLocMemOffset() gives the offset from the stack pointer
      SDValue PtrOff = DAG.getNode(ISD::ADD, DL, MVT::i16, StackPtr,
                                   DAG.getConstant(VA.getLocMemOffset(), DL, MVT::i16));

      // Store the argument to the stack
      SDValue Store = DAG.getStore(Chain, DL, Arg, PtrOff, MachinePointerInfo());
      MemOpChains.push_back(Store);
    }
  }

  // Chain all the store operations together
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOpChains);

  // Copy all of the result registers out of their physical registers
  for (auto &Reg : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, DL, Reg.first, Reg.second, InGlue);
    InGlue = Chain.getValue(1);
  }

  // If the callee is a GlobalAddress node, turn it into a TargetGlobalAddress
  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), DL, MVT::i16);
  else if (ExternalSymbolSDNode *E = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i16);

  // Build the call node
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  // Add argument registers
  for (auto &Reg : RegsToPass)
    Ops.push_back(DAG.getRegister(Reg.first, Reg.second.getValueType()));

  // For non-tail calls, add a register mask for call-clobbered registers.
  // Tail calls don't need this because the callee returns to our caller
  // directly (we're done executing at this point).
  if (!IsTailCall) {
    const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();
    const uint32_t *Mask = TRI->getCallPreservedMask(MF, CallConv);
    Ops.push_back(DAG.getRegisterMask(Mask));
  }

  if (InGlue.getNode())
    Ops.push_back(InGlue);

  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);

  if (IsTailCall) {
    MF.getFrameInfo().setHasTailCall();
    return DAG.getNode(TMS9900ISD::TAIL_CALL, DL, NodeTys, Ops);
  }

  Chain = DAG.getNode(TMS9900ISD::CALL, DL, NodeTys, Ops);
  InGlue = Chain.getValue(1);

  // Emit CALLSEQ_END to mark the end of the call sequence
  // This restores the stack pointer after the call
  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, InGlue, DL);
  InGlue = Chain.getValue(1);

  // Handle return values
  SmallVector<CCValAssign, 16> RVLocs;
  CCState RVInfo(CallConv, isVarArg, MF, RVLocs, *DAG.getContext());
  RVInfo.AnalyzeCallResult(Ins, RetCC_TMS9900);

  for (unsigned i = 0, e = RVLocs.size(); i != e; ++i) {
    CCValAssign &VA = RVLocs[i];
    SDValue RetValue = DAG.getCopyFromReg(Chain, DL, VA.getLocReg(),
                                          VA.getValVT(), InGlue);
    Chain = RetValue.getValue(1);
    InGlue = RetValue.getValue(2);
    InVals.push_back(RetValue);
  }

  return Chain;
}
