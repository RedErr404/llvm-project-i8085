//===-- TMS9900ISelLowering.h - TMS9900 DAG Lowering Interface --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that TMS9900 uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_TMS9900_TMS9900ISELLOWERING_H
#define LLVM_LIB_TARGET_TMS9900_TMS9900ISELLOWERING_H

#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/Instructions.h"

namespace llvm {

class TMS9900Subtarget;
class TMS9900TargetMachine;

namespace TMS9900ISD {
enum NodeType : unsigned {
  FIRST_NUMBER = ISD::BUILTIN_OP_END,

  /// Return from function. Returns R11 as link register.
  RET,

  /// RETI - Return from interrupt. Uses RTWP instruction.
  RETI,

  /// CALL - Function call.
  CALL,

  /// Wrapper - Wrap address for PIC/etc.
  Wrapper,

  /// CMP - Compare operation that sets status flags.
  CMP,

  /// SELECT_CC - Conditional select based on status flags.
  SELECT_CC,

  /// BR_CC - Conditional branch based on status flags.
  BR_CC,

  /// MPY - Unsigned multiply 16x16->32
  /// Takes two i16 operands, returns i32 (high:low)
  MPY,

  /// BYTE_STORE - Store byte from HIGH byte position of register
  /// TMS9900 MOVB uses the HIGH byte, so we need this custom node
  /// to properly handle byte stores. Takes (chain, value, ptr).
  /// The value should have the byte in the HIGH byte position.
  BYTE_STORE,

  /// BYTE_LOAD - Load byte into HIGH byte position of register
  /// TMS9900 MOVB loads into HIGH byte. Takes (chain, ptr).
  /// Returns value with byte in HIGH position, plus chain.
  BYTE_LOAD,

  /// TAIL_CALL - Tail call to a function.
  /// Used for tail call optimization where the callee returns directly
  /// to our caller. Emitted as B @callee instead of BL @callee.
  TAIL_CALL
};
} // end namespace TMS9900ISD

class TMS9900TargetLowering : public TargetLowering {
public:
  explicit TMS9900TargetLowering(const TargetMachine &TM,
                                  const TMS9900Subtarget &STI);

  /// LowerOperation - Provide custom lowering hooks for some operations.
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

  /// ReplaceNodeResults - Replace the results of node with an illegal result
  /// type with new values built out of custom code. Called during type
  /// legalization for nodes with Custom action.
  void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;

  /// getTargetNodeName - This method returns the name of a target specific
  /// DAG node.
  const char *getTargetNodeName(unsigned Opcode) const override;

  /// getSetCCResultType - Return the ISD::SETCC ValueType.
  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                         EVT VT) const override;

  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *BB) const override;

  /// getPostIndexedAddressParts - returns true by value, key, and offset
  /// if (AM == ISD::POST_INC) and the load/store has a post-increment form.
  bool getPostIndexedAddressParts(SDNode *N, SDNode *Op, SDValue &Base,
                                  SDValue &Offset,
                                  ISD::MemIndexedMode &AM,
                                  SelectionDAG &DAG) const override;

  //===--------------------------------------------------------------------===//
  // Atomics - TMS9900 is single-core with no caches, so all atomics
  // can be safely converted to regular memory operations.
  //===--------------------------------------------------------------------===//

  AtomicExpansionKind shouldExpandAtomicLoadInIR(LoadInst *LI) const override {
    return AtomicExpansionKind::NotAtomic;
  }

  AtomicExpansionKind shouldExpandAtomicStoreInIR(StoreInst *SI) const override {
    return AtomicExpansionKind::NotAtomic;
  }

  AtomicExpansionKind shouldExpandAtomicRMWInIR(AtomicRMWInst *AI) const override {
    return AtomicExpansionKind::NotAtomic;
  }

  AtomicExpansionKind shouldExpandAtomicCmpXchgInIR(AtomicCmpXchgInst *AI) const override {
    return AtomicExpansionKind::NotAtomic;
  }

  //===--------------------------------------------------------------------===//
  // Inline Assembly Support
  //===--------------------------------------------------------------------===//

  //===--------------------------------------------------------------------===//
  // Tail Call Optimization
  //===--------------------------------------------------------------------===//

  bool mayBeEmittedAsTailCall(const CallInst *CI) const override;

  /// computeKnownBitsForFrameIndex - Override to report no known bits for
  /// frame indices. TMS9900's prologue uses DECT+padding that can break
  /// alignment assumptions, so we cannot trust object alignment to predict
  /// the low bits of stack addresses.
  void computeKnownBitsForFrameIndex(int FIOp, KnownBits &Known,
                                     const MachineFunction &MF) const override;

  /// getConstraintType - Given a constraint letter, return the type of
  /// constraint it is for this target.
  ConstraintType getConstraintType(StringRef Constraint) const override;

  /// getRegForInlineAsmConstraint - Given a constraint and operand type,
  /// return the register class and register to use.
  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;

private:
  const TMS9900Subtarget &Subtarget;

  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool isVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &dl, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;

  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context) const override;

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &dl,
                      SelectionDAG &DAG) const override;

  SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;

  bool isEligibleForTailCallOptimization(
      CCState &CCInfo, CallLoweringInfo &CLI, MachineFunction &MF,
      const SmallVector<CCValAssign, 16> &ArgLocs) const;

  SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerJumpTable(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBlockAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerShiftParts(SDValue Op, SelectionDAG &DAG,
                          bool IsLeft, bool IsArithmetic) const;
  SDValue LowerShift32(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBRCOND(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBR_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSETCC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerLOAD(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSTORE(SDValue Op, SelectionDAG &DAG) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_TMS9900_TMS9900ISELLOWERING_H
