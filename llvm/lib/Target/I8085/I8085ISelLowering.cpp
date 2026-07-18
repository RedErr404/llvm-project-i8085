//===-- I8085ISelLowering.cpp - I8085 DAG Lowering Implementation -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that I8085 uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "I8085ISelLowering.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/raw_ostream.h"

#include <iostream>

#include "I8085.h"
#include "I8085MachineFunctionInfo.h"
#include "I8085Subtarget.h"
#include "I8085TargetMachine.h"
#include "MCTargetDesc/I8085MCTargetDesc.h"

namespace llvm {

static void fail(const SDLoc &DL, SelectionDAG &DAG, const Twine &Msg,
                 SDValue Val = {}) {
  std::string Str;
  if (Val) {
    raw_string_ostream OS(Str);
    Val->print(OS);
    OS << ' ';
  }
  MachineFunction &MF = DAG.getMachineFunction();
  DAG.getContext()->diagnose(DiagnosticInfoUnsupported(
      MF.getFunction(), Twine(Str).concat(Msg), DL.getDebugLoc()));
}

static void splitI64Value(SDValue Val, SDValue &Lo, SDValue &Hi,
                          SelectionDAG &DAG, const SDLoc &DL) {
  if (Val.getOpcode() == ISD::BUILD_PAIR) {
    Lo = Val.getOperand(0);
    Hi = Val.getOperand(1);
    return;
  }

  auto Parts = DAG.SplitScalar(Val, DL, MVT::i32, MVT::i32);
  Lo = Parts.first;
  Hi = Parts.second;
}

static SDValue buildI64Value(SDValue Lo, SDValue Hi,
                             SelectionDAG &DAG, const SDLoc &DL) {
  return DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i64, Lo, Hi);
}

static bool getPowerOf2ShiftAmount(const ConstantSDNode *C,
                                   unsigned &ShiftAmt) {
  if (!C)
    return false;
  const APInt &Val = C->getAPIntValue();
  if (!Val.isPowerOf2())
    return false;
  ShiftAmt = Val.logBase2();
  return true;
}

static bool isIntegerVT(EVT VT) {
  return VT.isInteger() && VT != MVT::i1;
}

static SDValue lowerI64Load(SDValue Op, SelectionDAG &DAG) {
  SDLoc DL(Op);
  auto *LD = cast<LoadSDNode>(Op.getNode());
  EVT MemVT = LD->getMemoryVT();
  SDValue Base = LD->getBasePtr();
  EVT PtrVT = Base.getValueType();
  Align Align = LD->getOriginalAlign();
  auto Flags = LD->getMemOperand()->getFlags();
  auto AAInfo = LD->getAAInfo();

  if (MemVT != MVT::i64) {
    SDValue Tmp = DAG.getLoad(MemVT, DL, LD->getChain(), Base,
                              LD->getPointerInfo(), Align, Flags, AAInfo);
    SDValue Ext;
    switch (LD->getExtensionType()) {
    case ISD::SEXTLOAD:
      Ext = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, Tmp);
      break;
    case ISD::ZEXTLOAD:
      Ext = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, Tmp);
      break;
    case ISD::EXTLOAD:
      Ext = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Tmp);
      break;
    default:
      Ext = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, Tmp);
      break;
    }
    return DAG.getMergeValues({Ext, Tmp.getValue(1)}, DL);
  }

  SDValue Lo = DAG.getLoad(MVT::i32, DL, LD->getChain(), Base,
                           LD->getPointerInfo(), Align, Flags, AAInfo);
  SDValue HiPtr = DAG.getNode(ISD::ADD, DL, PtrVT, Base,
                              DAG.getConstant(4, DL, PtrVT));
  SDValue Hi = DAG.getLoad(MVT::i32, DL, Lo.getValue(1), HiPtr,
                           LD->getPointerInfo().getWithOffset(4), Align,
                           Flags, AAInfo);

  SDValue Val = buildI64Value(Lo, Hi, DAG, DL);
  return DAG.getMergeValues({Val, Hi.getValue(1)}, DL);
}

static SDValue lowerI64Store(SDValue Op, SelectionDAG &DAG) {
  SDLoc DL(Op);
  auto *ST = cast<StoreSDNode>(Op.getNode());
  EVT MemVT = ST->getMemoryVT();
  SDValue Base = ST->getBasePtr();
  EVT PtrVT = Base.getValueType();
  Align Align = ST->getOriginalAlign();
  SDValue Val = ST->getValue();
  auto Flags = ST->getMemOperand()->getFlags();
  auto AAInfo = ST->getAAInfo();

  if (MemVT != MVT::i64) {
    SDValue Trunc = DAG.getNode(ISD::TRUNCATE, DL, MemVT, Val);
    return DAG.getStore(ST->getChain(), DL, Trunc, Base,
                        ST->getPointerInfo(), Align, Flags, AAInfo);
  }

  SDValue Lo, Hi;
  splitI64Value(Val, Lo, Hi, DAG, DL);

  SDValue StoreLo = DAG.getStore(ST->getChain(), DL, Lo, Base,
                                 ST->getPointerInfo(), Align, Flags, AAInfo);
  SDValue HiPtr = DAG.getNode(ISD::ADD, DL, PtrVT, Base,
                              DAG.getConstant(4, DL, PtrVT));
  SDValue StoreHi = DAG.getStore(StoreLo, DL, Hi, HiPtr,
                                 ST->getPointerInfo().getWithOffset(4), Align,
                                 Flags, AAInfo);
  return StoreHi;
}

I8085TargetLowering::I8085TargetLowering(const I8085TargetMachine &TM,
                                     const I8085Subtarget &STI)
    : TargetLowering(TM), Subtarget(STI) {
  // Set up the register classes.
  addRegisterClass(MVT::i8, &I8085::GR8RegClass);
  addRegisterClass(MVT::i16, &I8085::GR16RegClass);
  addRegisterClass(MVT::i32, &I8085::GR32RegClass);

  // Compute derived properties from the register classes.
  computeRegisterProperties(Subtarget.getRegisterInfo());

  setBooleanContents(ZeroOrOneBooleanContent);
  setBooleanVectorContents(ZeroOrOneBooleanContent);
  PredictableSelectIsExpensive = true;
  setSchedulingPreference(Sched::RegPressure);
  setStackPointerRegisterToSaveRestore(I8085::SP);
  setSupportsUnalignedAtomics(false);
  // Atomics are not supported; make them Custom so we can emit diagnostics.
  for (MVT VT : {MVT::i1, MVT::i8, MVT::i16, MVT::i32, MVT::i64}) {
    for (unsigned Op :
         {ISD::ATOMIC_LOAD, ISD::ATOMIC_STORE, ISD::ATOMIC_CMP_SWAP,
          ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS, ISD::ATOMIC_SWAP,
          ISD::ATOMIC_LOAD_ADD, ISD::ATOMIC_LOAD_SUB, ISD::ATOMIC_LOAD_AND,
          ISD::ATOMIC_LOAD_CLR, ISD::ATOMIC_LOAD_OR, ISD::ATOMIC_LOAD_XOR,
          ISD::ATOMIC_LOAD_NAND, ISD::ATOMIC_LOAD_MIN, ISD::ATOMIC_LOAD_MAX,
          ISD::ATOMIC_LOAD_UMIN, ISD::ATOMIC_LOAD_UMAX,
          ISD::ATOMIC_LOAD_FADD, ISD::ATOMIC_LOAD_FSUB,
          ISD::ATOMIC_LOAD_FMAX, ISD::ATOMIC_LOAD_FMIN,
          ISD::ATOMIC_LOAD_UINC_WRAP, ISD::ATOMIC_LOAD_UDEC_WRAP}) {
      setOperationAction(Op, VT, Custom);
    }
  }
  setOperationAction(ISD::ATOMIC_FENCE, MVT::Other, Custom);
  // Force custom memcpy/memset expansion in SelectionDAGInfo.
  MaxStoresPerMemcpy = 0;
  MaxStoresPerMemcpyOptSize = 0;
  MaxStoresPerMemmove = 0;
  MaxStoresPerMemmoveOptSize = 0;
  MaxStoresPerMemset = 0;
  MaxStoresPerMemsetOptSize = 0;

  setTruncStoreAction(MVT::i16, MVT::i8, Expand);
  setTruncStoreAction(MVT::i32, MVT::i8, Expand);
  setTruncStoreAction(MVT::i32, MVT::i16, Expand);
  // Let LLVM's type legalizer split i64 loads/stores into two i32 halves.
  // Custom lowering created BUILD_PAIR MVT::i64 which survived to ISel.
  setOperationAction(ISD::LOAD, MVT::i64, Expand);
  setOperationAction(ISD::STORE, MVT::i64, Expand);
  // Let LLVM's type legalizer handle i64 extends by splitting into i32 pairs.
  // Custom lowering would create BUILD_PAIR MVT::i64 which can't be selected
  // since i64 is not a register class.
  setOperationAction(ISD::ZERO_EXTEND, MVT::i64, Expand);
  setOperationAction(ISD::SIGN_EXTEND, MVT::i64, Expand);
  setOperationAction(ISD::ANY_EXTEND, MVT::i64, Expand);
  // TRUNCATE i32 is Legal — the type legalizer handles i64→i32 truncation
  // via ExpandOp_TRUNCATE before we reach operation legalization.

  // Ensure sign/zero/any extension to i32 uses efficient pseudo instructions
  // instead of being expanded to shift sequences.
  setOperationAction(ISD::SIGN_EXTEND, MVT::i32, Legal);
  setOperationAction(ISD::ZERO_EXTEND, MVT::i32, Legal);
  setOperationAction(ISD::ANY_EXTEND, MVT::i32, Legal);

  setOperationAction(ISD::FADD, MVT::f32, LibCall);
  setOperationAction(ISD::FSUB, MVT::f32, LibCall);
  setOperationAction(ISD::FMUL, MVT::f32, LibCall);
  setOperationAction(ISD::FDIV, MVT::f32, LibCall);
  setOperationAction(ISD::SETCC, MVT::f32, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::f32, Expand);
  setOperationAction(ISD::SELECT, MVT::f32, Expand);
  setOperationAction(ISD::FP_TO_SINT, MVT::i32, LibCall);
  setOperationAction(ISD::FP_TO_UINT, MVT::i32, LibCall);
  setOperationAction(ISD::SINT_TO_FP, MVT::i32, LibCall);
  setOperationAction(ISD::UINT_TO_FP, MVT::i32, LibCall);

  for (MVT VT : {MVT::i1, MVT::i8, MVT::i16, MVT::i32})
    setOperationAction(ISD::SIGN_EXTEND_INREG, VT, Custom);

  setTargetDAGCombine(ISD::ADD);
  setTargetDAGCombine(ISD::SUB);
  setTargetDAGCombine(ISD::AND);
  setTargetDAGCombine(ISD::OR);
  setTargetDAGCombine(ISD::XOR);
  setTargetDAGCombine(ISD::SHL);
  setTargetDAGCombine(ISD::SRL);
  setTargetDAGCombine(ISD::SRA);
  setTargetDAGCombine(ISD::MUL);
  setTargetDAGCombine(ISD::UDIV);
  setTargetDAGCombine(ISD::UREM);
  setTargetDAGCombine(ISD::TRUNCATE);

  for (MVT VT : MVT::integer_valuetypes()) {
    for (auto N : {ISD::EXTLOAD, ISD::SEXTLOAD, ISD::ZEXTLOAD}) {
      setLoadExtAction(N, VT, MVT::i1, Promote);
      setLoadExtAction(N, VT, MVT::i8, Expand);
      setLoadExtAction(N, VT, MVT::i16, Expand);
      setLoadExtAction(N, VT, MVT::i32, Expand);
    }
  }

  setOperationAction(ISD::MUL, MVT::i8, LibCall);
  setOperationAction(ISD::MUL, MVT::i16, Custom);
  setOperationAction(ISD::MUL, MVT::i32, Custom);
  setOperationAction(ISD::MUL, MVT::i64, Expand);
  for (MVT VT : {MVT::i8, MVT::i16, MVT::i32, MVT::i64}) {
    setOperationAction(ISD::MULHS, VT, Expand);
    setOperationAction(ISD::MULHU, VT, Expand);
    setOperationAction(ISD::SMUL_LOHI, VT, Expand);
    setOperationAction(ISD::UMUL_LOHI, VT, Expand);
  }

  setOperationAction(ISD::SDIV, MVT::i8, LibCall);
  setOperationAction(ISD::SDIV, MVT::i16, LibCall);
  setOperationAction(ISD::SDIV, MVT::i32, LibCall);
  setOperationAction(ISD::SDIV, MVT::i64, Expand);

  setOperationAction(ISD::SREM, MVT::i8, LibCall);
  setOperationAction(ISD::SREM, MVT::i16, LibCall);
  setOperationAction(ISD::SREM, MVT::i32, LibCall);
  setOperationAction(ISD::SREM, MVT::i64, Expand);

  setOperationAction(ISD::UDIV, MVT::i8, LibCall);
  setOperationAction(ISD::UDIV, MVT::i16, LibCall);
  setOperationAction(ISD::UDIV, MVT::i32, LibCall);
  setOperationAction(ISD::UDIV, MVT::i64, Expand);

  setOperationAction(ISD::UREM, MVT::i8, LibCall);
  setOperationAction(ISD::UREM, MVT::i16, LibCall);
  setOperationAction(ISD::UREM, MVT::i32, LibCall);
  setOperationAction(ISD::UREM, MVT::i64, Expand);

  // Combined DIVREM: emit a single __udivmod/__sdivmod call that returns
  // both quotient and remainder, avoiding two separate division loops.
  setOperationAction(ISD::UDIVREM, MVT::i8, Custom);
  setOperationAction(ISD::UDIVREM, MVT::i16, Custom);
  setOperationAction(ISD::UDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i64, Expand);
  setOperationAction(ISD::SDIVREM, MVT::i8, Custom);
  setOperationAction(ISD::SDIVREM, MVT::i16, Custom);
  setOperationAction(ISD::SDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::SDIVREM, MVT::i64, Expand);

  // Let LLVM's type legalizer expand i64 arithmetic into i32 pairs.
  // ADD/SUB use comparison-based carry detection (no ADDC/ADDE needed).
  // AND/OR/XOR split trivially into two i32 operations.
  setOperationAction(ISD::ADD, MVT::i64, Expand);
  setOperationAction(ISD::SUB, MVT::i64, Expand);

  setOperationAction(ISD::AND, MVT::i64, Expand);
  setOperationAction(ISD::OR, MVT::i64, Expand);
  setOperationAction(ISD::XOR, MVT::i64, Expand);

  // Hybrid i32 shifts: constant shifts are handled inline in ISel
  // (byte-shuffle + rotates), variable shifts go to library calls.
  setOperationAction(ISD::SHL, MVT::i32, Custom);
  setOperationAction(ISD::SRL, MVT::i32, Custom);
  setOperationAction(ISD::SRA, MVT::i32, Custom);

  setOperationAction(ISD::SHL, MVT::i64, Expand);
  setOperationAction(ISD::SRA, MVT::i64, Expand);
  setOperationAction(ISD::SRL, MVT::i64, Expand);
  setOperationAction(ISD::SHL_PARTS, MVT::i32, Expand);
  setOperationAction(ISD::SRA_PARTS, MVT::i32, Expand);
  setOperationAction(ISD::SRL_PARTS, MVT::i32, Expand);
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAEND, MVT::Other, Custom);
  setOperationAction(ISD::VACOPY, MVT::Other, Custom);
  setOperationAction(ISD::VAARG, MVT::Other, Expand);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i8, Expand);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i16, Expand);

  for (MVT VT : {MVT::i8, MVT::i16}) {
    setOperationAction(ISD::CTLZ, VT, Expand);
    setOperationAction(ISD::CTLZ_ZERO_UNDEF, VT, Expand);
    setOperationAction(ISD::CTTZ, VT, Expand);
    setOperationAction(ISD::CTTZ_ZERO_UNDEF, VT, Expand);
    setOperationAction(ISD::CTPOP, VT, Expand);
  }
  setOperationAction(ISD::BITREVERSE, MVT::i8, Expand);
  setOperationAction(ISD::BITREVERSE, MVT::i16, Expand);
  setOperationAction(ISD::BITREVERSE, MVT::i32, Expand);
  setOperationAction(ISD::BITREVERSE, MVT::i64, Expand);
  // BSWAP is implemented via pseudo instructions for i16 and i32.
  // These expand to simple byte moves which is much more efficient than
  // the default shift-based expansion.
  setOperationAction(ISD::BSWAP, MVT::i16, Legal);
  setOperationAction(ISD::BSWAP, MVT::i32, Legal);
  setOperationAction(ISD::BSWAP, MVT::i64, Expand);

  // Tier 3: Rotates — Custom for all widths
  setOperationAction(ISD::ROTL, MVT::i8, Custom);
  setOperationAction(ISD::ROTR, MVT::i8, Custom);
  setOperationAction(ISD::ROTL, MVT::i16, Custom);
  setOperationAction(ISD::ROTR, MVT::i16, Custom);
  setOperationAction(ISD::ROTL, MVT::i32, Custom);
  setOperationAction(ISD::ROTR, MVT::i32, Custom);
  setOperationAction(ISD::ROTL, MVT::i64, Expand);
  setOperationAction(ISD::ROTR, MVT::i64, Expand);

  // Custom lowering for funnel shifts - efficient for specific cases
  setOperationAction(ISD::FSHL, MVT::i8, Custom);
  setOperationAction(ISD::FSHL, MVT::i16, Custom);
  setOperationAction(ISD::FSHR, MVT::i8, Custom);
  setOperationAction(ISD::FSHR, MVT::i16, Custom);
  // Expand larger funnel shifts to shift + or (the default expansion).
  // i32 Custom adds libcall path for variable amounts.
  setOperationAction(ISD::FSHL, MVT::i32, Expand);
  setOperationAction(ISD::FSHR, MVT::i32, Expand);
  setOperationAction(ISD::FSHL, MVT::i64, Expand);
  setOperationAction(ISD::FSHR, MVT::i64, Expand);

  // Bit counting
  setOperationAction(ISD::CTLZ, MVT::i32, Custom);
  setOperationAction(ISD::CTLZ_ZERO_UNDEF, MVT::i32, Custom);
  setOperationAction(ISD::CTLZ, MVT::i64, Custom);
  setOperationAction(ISD::CTLZ_ZERO_UNDEF, MVT::i64, Custom);
  setOperationAction(ISD::CTTZ, MVT::i32, Custom);
  setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::i32, Custom);
  setOperationAction(ISD::CTTZ, MVT::i64, Custom);
  setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::i64, Custom);
  // CTPOP i32 via Custom (manual libcall), i64 via Expand
  setOperationAction(ISD::CTPOP, MVT::i32, Custom);
  setOperationAction(ISD::CTPOP, MVT::i64, Expand);

  // Custom lowering for ABS to avoid expensive shift-based expansion.
  // We expand abs(x) to: x < 0 ? -x : x
  for (MVT VT : {MVT::i8, MVT::i16, MVT::i32})
    setOperationAction(ISD::ABS, VT, Custom);
  setOperationAction(ISD::ABS, MVT::i64, Expand);

  // Tier 4: Saturating arithmetic — Custom for i8/i16/i32, Expand for i64
  for (MVT VT : {MVT::i8, MVT::i16, MVT::i32}) {
    setOperationAction(ISD::UADDSAT, VT, Custom);
    setOperationAction(ISD::SADDSAT, VT, Custom);
    setOperationAction(ISD::USUBSAT, VT, Custom);
    setOperationAction(ISD::SSUBSAT, VT, Custom);
  }

  // Tier 5: Overflow detection — Custom for i8/i16/i32, Expand for i64
  for (MVT VT : {MVT::i8, MVT::i16, MVT::i32}) {
    setOperationAction(ISD::UADDO, VT, Custom);
    setOperationAction(ISD::SADDO, VT, Custom);
    setOperationAction(ISD::USUBO, VT, Custom);
    setOperationAction(ISD::SSUBO, VT, Custom);
    setOperationAction(ISD::UMULO, VT, Custom);
    setOperationAction(ISD::SMULO, VT, Custom);
  }

  for (MVT VT : {MVT::i8, MVT::i16, MVT::i32}) {
    setOperationAction(ISD::SELECT, VT, Legal);
    setOperationAction(ISD::SELECT_CC, VT, Custom);
  }
  setOperationAction(ISD::SELECT, MVT::i64, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i64, Expand);

  setLibcallName(RTLIB::MUL_I8, "__mul8");
  setLibcallName(RTLIB::MUL_I16, "__mul16");
  setLibcallName(RTLIB::MUL_I32, "__mul32");
  setLibcallName(RTLIB::MUL_I64, "__muldi3");

  setLibcallName(RTLIB::SDIV_I8, "__sdiv8");
  setLibcallName(RTLIB::SDIV_I16, "__sdiv16");
  setLibcallName(RTLIB::SDIV_I32, "__sdiv32");
  setLibcallName(RTLIB::SDIV_I64, "__divdi3");

  setLibcallName(RTLIB::SREM_I8, "__srem8");
  setLibcallName(RTLIB::SREM_I16, "__srem16");
  setLibcallName(RTLIB::SREM_I32, "__srem32");
  setLibcallName(RTLIB::SREM_I64, "__moddi3");

  setLibcallName(RTLIB::UDIV_I8, "__udiv8");
  setLibcallName(RTLIB::UDIV_I16, "__udiv16");
  setLibcallName(RTLIB::UDIV_I32, "__udiv32");
  setLibcallName(RTLIB::UDIV_I64, "__udivdi3");

  setLibcallName(RTLIB::UREM_I8, "__urem8");
  setLibcallName(RTLIB::UREM_I16, "__urem16");
  setLibcallName(RTLIB::UREM_I32, "__urem32");
  setLibcallName(RTLIB::UREM_I64, "__umoddi3");

  setLibcallName(RTLIB::UDIVREM_I8, "__udivmod8");
  setLibcallName(RTLIB::UDIVREM_I16, "__udivmod16");
  setLibcallName(RTLIB::SDIVREM_I8, "__sdivmod8");
  setLibcallName(RTLIB::SDIVREM_I16, "__sdivmod16");

  // These combined div/rem helpers return quotient and remainder directly in
  // registers, not via a C ABI struct return slot.
  setLibcallCallingConv(RTLIB::UDIVREM_I8, CallingConv::I8085_BUILTIN);
  setLibcallCallingConv(RTLIB::UDIVREM_I16, CallingConv::I8085_BUILTIN);
  setLibcallCallingConv(RTLIB::SDIVREM_I8, CallingConv::I8085_BUILTIN);
  setLibcallCallingConv(RTLIB::SDIVREM_I16, CallingConv::I8085_BUILTIN);

  setLibcallName(RTLIB::ADD_F32, "__addsf3");
  setLibcallName(RTLIB::SUB_F32, "__subsf3");
  setLibcallName(RTLIB::MUL_F32, "__mulsf3");
  setLibcallName(RTLIB::DIV_F32, "__divsf3");
  setLibcallName(RTLIB::SINTTOFP_I32_F32, "__floatsisf");
  setLibcallName(RTLIB::UINTTOFP_I32_F32, "__floatunsisf");
  setLibcallName(RTLIB::FPTOSINT_F32_I32, "__fixsfsi");
  setLibcallName(RTLIB::FPTOUINT_F32_I32, "__fixunssfsi");
  setLibcallName(RTLIB::OEQ_F32, "__eqsf2");
  setLibcallName(RTLIB::UNE_F32, "__nesf2");
  setLibcallName(RTLIB::OLT_F32, "__ltsf2");
  setLibcallName(RTLIB::OLE_F32, "__lesf2");
  setLibcallName(RTLIB::OGT_F32, "__gtsf2");
  setLibcallName(RTLIB::OGE_F32, "__gesf2");
  setLibcallName(RTLIB::UO_F32, "__unordsf2");

  setLibcallName(RTLIB::CTLZ_I32, "__clzsi2");
  setLibcallName(RTLIB::CTLZ_I64, "__clzdi2");
  setLibcallName(RTLIB::CTTZ_I32, "__ctzsi2");
  setLibcallName(RTLIB::CTTZ_I64, "__ctzdi2");

  setLibcallName(RTLIB::SHL_I32, "__ashlsi3");
  setLibcallName(RTLIB::SRL_I32, "__lshrsi3");
  setLibcallName(RTLIB::SRA_I32, "__ashrsi3");
  setLibcallName(RTLIB::SHL_I64, "__ashldi3");
  setLibcallName(RTLIB::SRL_I64, "__lshrdi3");
  setLibcallName(RTLIB::SRA_I64, "__ashrdi3");

  setOperationAction(ISD::GlobalAddress, MVT::i16, Custom);
  setOperationAction(ISD::BlockAddress, MVT::i16, Custom);
  setOperationAction(ISD::ConstantPool, MVT::i16, Custom);
  setOperationAction(ISD::JumpTable, MVT::i16, Custom);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);

  setMinFunctionAlignment(Align(2));
  // The 8085 can materialize an indirect branch via PCHL, and MC lowering
  // already supports jump-table symbols. Keep the threshold conservative so
  // small switches still become compare chains, but allow large dispatchers
  // such as bytecode interpreters to use jump tables.
  setMinimumJumpTableEntries(8);
  setMaxAtomicSizeInBitsSupported(0);
}

const char *I8085TargetLowering::getTargetNodeName(unsigned Opcode) const {
#define NODE(name)                                                             \
  case I8085ISD::name:                                                           \
    return #name

  switch (Opcode) {
  default:
    return nullptr;
    NODE(RET_FLAG);
    NODE(RETI_FLAG);
    NODE(CALL);
    NODE(TC_RETURN);
    NODE(WRAPPER);
    NODE(LSL);
    NODE(LSR);
    NODE(ROL);
    NODE(ROR);
    NODE(ASR);
    NODE(BRCOND);
    NODE(CMP);
    NODE(CMPC);
    NODE(TST);
    NODE(SELECT_CC);
    NODE(TRUNC32_HI);
    NODE(PACK_CALL_RESULT_32);
    NODE(MUL_IMM);
#undef NODE
  }
}

TargetLowering::ConstraintType
I8085TargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
      return C_RegisterClass;
    case 'I':
      return C_Immediate;
    default:
      break;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

std::pair<unsigned, const TargetRegisterClass *>
I8085TargetLowering::getRegForInlineAsmConstraint(
    const TargetRegisterInfo *TRI, StringRef Constraint, MVT VT) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
      if (VT == MVT::i8)
        return std::make_pair(0U, &I8085::GR8RegClass);
      if (VT == MVT::i16)
        return std::make_pair(0U, &I8085::GR16RegClass);
      if (VT == MVT::i32)
        return std::make_pair(0U, &I8085::GR32RegClass);
      break;
    default:
      break;
    }
  }

  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

void I8085TargetLowering::LowerAsmOperandForConstraint(
    SDValue Op, StringRef Constraint, std::vector<SDValue> &Ops,
    SelectionDAG &DAG) const {
  if (Constraint.size() != 1)
    return;

  switch (Constraint[0]) {
  default:
    break;
  case 'I': {
    const ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op);
    if (!C)
      return;

    uint64_t Val = C->getZExtValue();
    if (!isUInt<8>(Val))
      return;

    Ops.push_back(DAG.getTargetConstant(Val, SDLoc(Op), Op.getValueType()));
    return;
  }
  }

  TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops, DAG);
}

EVT I8085TargetLowering::getSetCCResultType(const DataLayout &DL, LLVMContext &,
                                          EVT VT) const {
  assert(!VT.isVector() && "No I8085 SetCC type for vectors!");
  return MVT::i8;
}

void I8085TargetLowering::computeKnownBitsForFrameIndex(
    int FrameIdx, KnownBits &Known, const MachineFunction &MF) const {
  // Clamp object alignment to the ABI stack alignment so we don't assume
  // stronger alignment than the runtime provides.
  Align ObjAlign = MF.getFrameInfo().getObjectAlign(FrameIdx);
  Align StackAlign = MF.getSubtarget<I8085Subtarget>()
                         .getFrameLowering()
                         ->getStackAlign();
  Align KnownAlign = std::min(ObjAlign, StackAlign);
  if (KnownAlign > Align(1))
    Known.Zero.setLowBits(Log2(KnownAlign));
}

SDValue I8085TargetLowering::LowerDivRem(SDValue Op, SelectionDAG &DAG) const {
  unsigned Opcode = Op->getOpcode();
  assert((Opcode == ISD::SDIVREM || Opcode == ISD::UDIVREM) &&
         "Invalid opcode for Div/Rem lowering");
  bool IsSigned = (Opcode == ISD::SDIVREM);
  EVT VT = Op->getValueType(0);
  Type *Ty = VT.getTypeForEVT(*DAG.getContext());

  RTLIB::Libcall LC;
  switch (VT.getSimpleVT().SimpleTy) {
  default:
    llvm_unreachable("Unexpected request for libcall!");
  case MVT::i8:
    LC = IsSigned ? RTLIB::SDIVREM_I8 : RTLIB::UDIVREM_I8;
    break;
  case MVT::i16:
    LC = IsSigned ? RTLIB::SDIVREM_I16 : RTLIB::UDIVREM_I16;
    break;
  }

  SDValue InChain = DAG.getEntryNode();

  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;
  for (SDValue const &Value : Op->op_values()) {
    Entry.Node = Value;
    Entry.Ty = Value.getValueType().getTypeForEVT(*DAG.getContext());
    Entry.IsSExt = IsSigned;
    Entry.IsZExt = !IsSigned;
    Args.push_back(Entry);
  }

  SDValue Callee = DAG.getExternalSymbol(getLibcallName(LC),
                                         getPointerTy(DAG.getDataLayout()));

  // The divmod routines return a struct {quotient, remainder} where both
  // elements have the same type as the operands.
  Type *RetTy = (Type *)StructType::get(Ty, Ty);

  SDLoc dl(Op);
  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl)
      .setChain(InChain)
      .setLibCallee(getLibcallCallingConv(LC), RetTy, Callee, std::move(Args))
      .setInRegister()
      .setSExtResult(IsSigned)
      .setZExtResult(!IsSigned);

  std::pair<SDValue, SDValue> CallInfo = LowerCallTo(CLI);
  return CallInfo.first;
}

SDValue I8085TargetLowering::LowerGlobalAddress(SDValue Op,
                                              SelectionDAG &DAG) const {
  auto DL = DAG.getDataLayout();

  const GlobalValue *GV = cast<GlobalAddressSDNode>(Op)->getGlobal();
  int64_t Offset = cast<GlobalAddressSDNode>(Op)->getOffset();

  // Create the TargetGlobalAddress node, folding in the constant offset.
  SDValue Result =
      DAG.getTargetGlobalAddress(GV, SDLoc(Op), getPointerTy(DL), Offset);
  return DAG.getNode(I8085ISD::WRAPPER, SDLoc(Op), getPointerTy(DL), Result);
}

SDValue I8085TargetLowering::LowerBlockAddress(SDValue Op,
                                             SelectionDAG &DAG) const {
  auto DL = DAG.getDataLayout();
  const BlockAddress *BA = cast<BlockAddressSDNode>(Op)->getBlockAddress();

  SDValue Result = DAG.getTargetBlockAddress(BA, getPointerTy(DL));

  return DAG.getNode(I8085ISD::WRAPPER, SDLoc(Op), getPointerTy(DL), Result);
}

SDValue I8085TargetLowering::LowerConstantPool(SDValue Op,
                                              SelectionDAG &DAG) const {
  auto DL = DAG.getDataLayout();
  const ConstantPoolSDNode *CP = cast<ConstantPoolSDNode>(Op);

  SDValue Result =
      DAG.getTargetConstantPool(CP->getConstVal(), getPointerTy(DL),
                                CP->getAlign(), CP->getOffset());

  return DAG.getNode(I8085ISD::WRAPPER, SDLoc(Op), getPointerTy(DL), Result);
}

SDValue I8085TargetLowering::LowerJumpTable(SDValue Op,
                                            SelectionDAG &DAG) const {
  auto DL = DAG.getDataLayout();
  const auto *JT = cast<JumpTableSDNode>(Op);

  SDValue Result = DAG.getTargetJumpTable(JT->getIndex(), getPointerTy(DL));
  return DAG.getNode(I8085ISD::WRAPPER, SDLoc(Op), getPointerTy(DL), Result);
}

/// Returns appropriate CP/CPI/CPC nodes code for the given 8/16-bit operands.
SDValue I8085TargetLowering::getI8085Cmp(SDValue LHS, SDValue RHS,
                                     SelectionDAG &DAG, SDLoc DL) const {
  assert((LHS.getSimpleValueType() == RHS.getSimpleValueType()) &&
         "LHS and RHS have different types");
  assert(((LHS.getSimpleValueType() == MVT::i16) ||
          (LHS.getSimpleValueType() == MVT::i8)) &&
         "invalid comparison type");

  SDValue Cmp;

  if (LHS.getSimpleValueType() == MVT::i16 && isa<ConstantSDNode>(RHS)) {
    // Generate a CPI/CPC pair if RHS is a 16-bit constant.
    SDValue LHSlo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i8, LHS,
                                DAG.getIntPtrConstant(0, DL));
    SDValue LHShi = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i8, LHS,
                                DAG.getIntPtrConstant(1, DL));
    SDValue RHSlo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i8, RHS,
                                DAG.getIntPtrConstant(0, DL));
    SDValue RHShi = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i8, RHS,
                                DAG.getIntPtrConstant(1, DL));
    Cmp = DAG.getNode(I8085ISD::CMP, DL, MVT::Glue, LHSlo, RHSlo);
    Cmp = DAG.getNode(I8085ISD::CMPC, DL, MVT::Glue, LHShi, RHShi, Cmp);
  } else {
    // Generate ordinary 16-bit comparison.
    Cmp = DAG.getNode(I8085ISD::CMP, DL, MVT::Glue, LHS, RHS);
  }

  return Cmp;
}

SDValue I8085TargetLowering::LowerShiftI64(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Val = Op.getOperand(0);
  SDValue Amt = Op.getOperand(1);

  if (Amt.getValueType() != MVT::i32)
    Amt = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i32, Amt);

  SDValue Mask6 = DAG.getConstant(63, DL, MVT::i32);
  SDValue Mask5 = DAG.getConstant(31, DL, MVT::i32);
  SDValue Zero = DAG.getConstant(0, DL, MVT::i32);
  SDValue Const32 = DAG.getConstant(32, DL, MVT::i32);
  SDValue Const31 = DAG.getConstant(31, DL, MVT::i32);

  SDValue AmtMasked = DAG.getNode(ISD::AND, DL, MVT::i32, Amt, Mask6);
  SDValue AmtLo = DAG.getNode(ISD::AND, DL, MVT::i32, AmtMasked, Mask5);
  SDValue InvAmt = DAG.getNode(ISD::SUB, DL, MVT::i32, Const32, AmtLo);

  SDValue Lo;
  SDValue Hi;
  splitI64Value(Val, Lo, Hi, DAG, DL);

  SDValue Lo1;
  SDValue Hi1;
  SDValue Lo2;
  SDValue Hi2;

  switch (Op.getOpcode()) {
  case ISD::SHL:
    Lo1 = DAG.getNode(ISD::SHL, DL, MVT::i32, Lo, AmtLo);
    Hi1 = DAG.getNode(ISD::OR, DL, MVT::i32,
                      DAG.getNode(ISD::SHL, DL, MVT::i32, Hi, AmtLo),
                      DAG.getNode(ISD::SRL, DL, MVT::i32, Lo, InvAmt));
    Lo2 = Zero;
    Hi2 = DAG.getNode(ISD::SHL, DL, MVT::i32, Lo, AmtLo);
    break;
  case ISD::SRL:
    Lo1 = DAG.getNode(ISD::OR, DL, MVT::i32,
                      DAG.getNode(ISD::SRL, DL, MVT::i32, Lo, AmtLo),
                      DAG.getNode(ISD::SHL, DL, MVT::i32, Hi, InvAmt));
    Hi1 = DAG.getNode(ISD::SRL, DL, MVT::i32, Hi, AmtLo);
    Lo2 = DAG.getNode(ISD::SRL, DL, MVT::i32, Hi, AmtLo);
    Hi2 = Zero;
    break;
  case ISD::SRA:
    Lo1 = DAG.getNode(ISD::OR, DL, MVT::i32,
                      DAG.getNode(ISD::SRL, DL, MVT::i32, Lo, AmtLo),
                      DAG.getNode(ISD::SHL, DL, MVT::i32, Hi, InvAmt));
    Hi1 = DAG.getNode(ISD::SRA, DL, MVT::i32, Hi, AmtLo);
    Lo2 = DAG.getNode(ISD::SRA, DL, MVT::i32, Hi, AmtLo);
    Hi2 = DAG.getNode(ISD::SRA, DL, MVT::i32, Hi, Const31);
    break;
  default:
    llvm_unreachable("Unexpected shift opcode");
  }

  SDValue Ge32 = DAG.getSetCC(DL, MVT::i1, AmtMasked, Const32, ISD::SETUGE);
  SDValue ZeroAmt = DAG.getSetCC(DL, MVT::i1, AmtMasked, Zero, ISD::SETEQ);

  SDValue LoSel = DAG.getSelect(DL, MVT::i32, Ge32, Lo2, Lo1);
  SDValue HiSel = DAG.getSelect(DL, MVT::i32, Ge32, Hi2, Hi1);
  SDValue LoRes = DAG.getSelect(DL, MVT::i32, ZeroAmt, Lo, LoSel);
  SDValue HiRes = DAG.getSelect(DL, MVT::i32, ZeroAmt, Hi, HiSel);

  return buildI64Value(LoRes, HiRes, DAG, DL);
}

/// Check if a value is effectively (sext i16 to i32).
/// If so, set Src to the original i16 value and return true.
/// Handles: SIGN_EXTEND, SIGN_EXTEND_INREG, and sign-extending loads.
/// For loads, Src is set to a TRUNCATE of the load to i16 (which is free
/// on i8085 since we just take the low 16 bits).
static bool isSExtFromI16(SDValue V, SDValue &Src) {
  if (V.getOpcode() == ISD::SIGN_EXTEND && V.getValueType() == MVT::i32 &&
      V.getOperand(0).getValueType() == MVT::i16) {
    Src = V.getOperand(0);
    return true;
  }
  // Match sign_extend_inreg(anyext/zext i16 to i32, i16)
  if (V.getOpcode() == ISD::SIGN_EXTEND_INREG && V.getValueType() == MVT::i32) {
    EVT ExtVT = cast<VTSDNode>(V.getOperand(1))->getVT();
    if (ExtVT == MVT::i16) {
      SDValue Inner = V.getOperand(0);
      if (Inner.getOpcode() == ISD::ZERO_EXTEND ||
          Inner.getOpcode() == ISD::ANY_EXTEND) {
        if (Inner.getOperand(0).getValueType() == MVT::i16) {
          Src = Inner.getOperand(0);
          return true;
        }
      }
      Src = SDValue();
      return false;
    }
  }
  return false;
}

/// Check if N is (mul (sext i16 to i32), (sext i16 to i32)).
/// If so, return the two i16 sources via A and B.
static bool isMulSExtI16(SDValue N, SDValue &A, SDValue &B) {
  if (N.getOpcode() != ISD::MUL || N.getValueType() != MVT::i32)
    return false;
  return isSExtFromI16(N.getOperand(0), A) &&
         isSExtFromI16(N.getOperand(1), B);
}

/// Emit a library call taking two i16 args and returning RetVT.
/// The callee is identified by the given symbol name.
static SDValue emitMul16LibCall(SelectionDAG &DAG, const SDLoc &DL,
                                const char *Name, EVT RetVT,
                                SDValue Arg0, SDValue Arg1,
                                const I8085TargetLowering &TLI) {
  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;

  Type *I16Ty = Type::getInt16Ty(*DAG.getContext());

  Entry.Node = Arg0;
  Entry.Ty = I16Ty;
  Args.push_back(Entry);

  Entry.Node = Arg1;
  Entry.Ty = I16Ty;
  Args.push_back(Entry);

  Type *RetTy = RetVT.getTypeForEVT(*DAG.getContext());
  auto PtrVT = TLI.getPointerTy(DAG.getDataLayout());
  SDValue Callee = DAG.getExternalSymbol(Name, PtrVT);

  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(DL)
      .setChain(DAG.getEntryNode())
      .setLibCallee(CallingConv::C, RetTy, Callee, std::move(Args));

  std::pair<SDValue, SDValue> CallResult = TLI.LowerCallTo(CLI);
  return CallResult.first;
}

/// Check if a value is effectively (sext i8 to i16).
/// If so, set Src to the original i8 value and return true.
/// Handles: SIGN_EXTEND, SIGN_EXTEND_INREG, and sign-extending loads.
static bool isSExtFromI8(SDValue V, SDValue &Src) {
  if (V.getOpcode() == ISD::SIGN_EXTEND && V.getValueType() == MVT::i16 &&
      V.getOperand(0).getValueType() == MVT::i8) {
    Src = V.getOperand(0);
    return true;
  }
  // Match sign_extend_inreg(anyext/zext i8 to i16, i8)
  if (V.getOpcode() == ISD::SIGN_EXTEND_INREG && V.getValueType() == MVT::i16) {
    EVT ExtVT = cast<VTSDNode>(V.getOperand(1))->getVT();
    if (ExtVT == MVT::i8) {
      SDValue Inner = V.getOperand(0);
      if (Inner.getOpcode() == ISD::ZERO_EXTEND ||
          Inner.getOpcode() == ISD::ANY_EXTEND) {
        if (Inner.getOperand(0).getValueType() == MVT::i8) {
          Src = Inner.getOperand(0);
          return true;
        }
      }
      Src = SDValue();
      return false;
    }
  }
  return false;
}

/// Check if N is (mul (sext i8 to i16), (sext i8 to i16)).
/// If so, return the two i8 sources via A and B.
static bool isMulSExtI8(SDValue N, SDValue &A, SDValue &B) {
  if (N.getOpcode() != ISD::MUL || N.getValueType() != MVT::i16)
    return false;
  return isSExtFromI8(N.getOperand(0), A) &&
         isSExtFromI8(N.getOperand(1), B);
}

/// Check if a value is (zext i8 to i16)
static bool isZExtFromI8(SDValue V, SDValue &Src) {
  if (V.getOpcode() == ISD::ZERO_EXTEND ||
      V.getOpcode() == ISD::ANY_EXTEND) {
    if (V.getOperand(0).getValueType() == MVT::i8) {
      Src = V.getOperand(0);
      return true;
    }
  }
  // Also match (and x, 0xFF) pattern
  if (V.getOpcode() == ISD::AND) {
    if (auto *Mask = dyn_cast<ConstantSDNode>(V.getOperand(1))) {
      if (Mask->getZExtValue() == 0xFF) {
        Src = V.getOperand(0);
        return true;
      }
    }
  }
  return false;
}

/// Emit a library call taking two i8 args and returning RetVT.
/// The callee is identified by the given symbol name.
static SDValue emitMul8LibCall(SelectionDAG &DAG, const SDLoc &DL,
                               const char *Name, EVT RetVT,
                               SDValue Arg0, SDValue Arg1,
                               const I8085TargetLowering &TLI) {
  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;

  Type *I8Ty = Type::getInt8Ty(*DAG.getContext());

  Entry.Node = Arg0;
  Entry.Ty = I8Ty;
  Args.push_back(Entry);

  Entry.Node = Arg1;
  Entry.Ty = I8Ty;
  Args.push_back(Entry);

  Type *RetTy = RetVT.getTypeForEVT(*DAG.getContext());
  auto PtrVT = TLI.getPointerTy(DAG.getDataLayout());
  SDValue Callee = DAG.getExternalSymbol(Name, PtrVT);

  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(DL)
      .setChain(DAG.getEntryNode())
      .setLibCallee(CallingConv::C, RetTy, Callee, std::move(Args));

  std::pair<SDValue, SDValue> CallResult = TLI.LowerCallTo(CLI);
  return CallResult.first;
}

/// Check if a value is effectively (sext i32 to i64).
/// If so, set Src to the original i32 value and return true.
/// Handles: SIGN_EXTEND, SIGN_EXTEND_INREG, and the BUILD_PAIR pattern
/// produced by LowerOperation for SIGN_EXTEND i64 on i8085:
///   BUILD_PAIR(Lo, select(Lo < 0, -1, 0))
static bool isSExtFromI32(SDValue V, SDValue &Src) {
  // Look through MERGE_VALUES which the type legalizer inserts.
  if (V.getOpcode() == ISD::MERGE_VALUES)
    return isSExtFromI32(V.getOperand(V.getResNo()), Src);

  if (V.getOpcode() == ISD::SIGN_EXTEND && V.getValueType() == MVT::i64 &&
      V.getOperand(0).getValueType() == MVT::i32) {
    Src = V.getOperand(0);
    return true;
  }
  // Match sign_extend_inreg(anyext/zext i32 to i64, i32)
  if (V.getOpcode() == ISD::SIGN_EXTEND_INREG && V.getValueType() == MVT::i64) {
    EVT ExtVT = cast<VTSDNode>(V.getOperand(1))->getVT();
    if (ExtVT == MVT::i32) {
      SDValue Inner = V.getOperand(0);
      if (Inner.getOpcode() == ISD::ZERO_EXTEND ||
          Inner.getOpcode() == ISD::ANY_EXTEND) {
        if (Inner.getOperand(0).getValueType() == MVT::i32) {
          Src = Inner.getOperand(0);
          return true;
        }
      }
      Src = SDValue();
      return false;
    }
  }
  // Match BUILD_PAIR(Lo, Hi) where Hi is the sign-extension of Lo.
  // This is how the i8085 backend lowers sext i32 to i64:
  //   Hi = select(setcc(Lo, 0, SETLT), -1, 0)
  // After legalization the SETCC condition may be further lowered,
  // so we don't require the condition to be a raw SETCC node.
  // We only require that the SELECT produces either -1 or 0.
  if (V.getOpcode() == ISD::BUILD_PAIR && V.getValueType() == MVT::i64) {
    SDValue Lo = V.getOperand(0);
    SDValue Hi = V.getOperand(1);
    if (Lo.getValueType() == MVT::i32 && Hi.getValueType() == MVT::i32) {
      // Pattern 1: Hi = SELECT(cond, TrueVal, FalseVal)
      // where {TrueVal, FalseVal} is {-1, 0} or {0, -1}.
      // This ensures the upper 32 bits are all-ones or all-zeros,
      // which is the sign extension of the lower 32 bits.
      if (Hi.getOpcode() == ISD::SELECT) {
        auto *TrueC = dyn_cast<ConstantSDNode>(Hi.getOperand(1));
        auto *FalseC = dyn_cast<ConstantSDNode>(Hi.getOperand(2));
        if (TrueC && FalseC) {
          bool TrueIsNeg1 = TrueC->isAllOnes();
          bool TrueIsZero = TrueC->isZero();
          bool FalseIsNeg1 = FalseC->isAllOnes();
          bool FalseIsZero = FalseC->isZero();
          if ((TrueIsNeg1 && FalseIsZero) || (TrueIsZero && FalseIsNeg1)) {
            Src = Lo;
            return true;
          }
        }
      }
      // Pattern 2: Hi = SRA(Lo, 31)
      if (Hi.getOpcode() == ISD::SRA) {
        if (Hi.getOperand(0) == Lo) {
          auto *ShAmtNode = dyn_cast<ConstantSDNode>(Hi.getOperand(1));
          if (ShAmtNode && ShAmtNode->getZExtValue() == 31) {
            Src = Lo;
            return true;
          }
        }
      }
      // Pattern 3: Hi = Constant
      // If both Lo and Hi are constants, the sign extension was
      // constant-folded. Check that Hi is the sign extension of Lo.
      if (auto *HiC = dyn_cast<ConstantSDNode>(Hi)) {
        if (auto *LoC = dyn_cast<ConstantSDNode>(Lo)) {
          int32_t LoVal = LoC->getSExtValue();
          int32_t HiVal = HiC->getSExtValue();
          if (HiVal == (LoVal >> 31)) {
            Src = Lo;
            return true;
          }
        }
      }
    }
  }
  return false;
}

/// Check if N is (mul (sext i32 to i64), (sext i32 to i64)).
/// If so, return the two i32 sources via A and B.
static bool isMulSExtI32(SDValue N, SDValue &A, SDValue &B) {
  if (N.getOpcode() != ISD::MUL || N.getValueType() != MVT::i64)
    return false;
  return isSExtFromI32(N.getOperand(0), A) &&
         isSExtFromI32(N.getOperand(1), B);
}

/// Check if a value is effectively (zext i16 to i32).
/// If so, set Src to the original i16 value and return true.
/// Handles: ZERO_EXTEND, ANY_EXTEND, (and x, 0xFFFF), and zero-extending loads.
static bool isZExtFromI16(SDValue V, SDValue &Src) {
  if (V.getOpcode() == ISD::ZERO_EXTEND ||
      V.getOpcode() == ISD::ANY_EXTEND) {
    if (V.getOperand(0).getValueType() == MVT::i16) {
      Src = V.getOperand(0);
      return true;
    }
  }
  // Match (and x, 0xFFFF) pattern
  if (V.getOpcode() == ISD::AND && V.getValueType() == MVT::i32) {
    if (auto *Mask = dyn_cast<ConstantSDNode>(V.getOperand(1))) {
      if (Mask->getZExtValue() == 0xFFFF) {
        Src = V.getOperand(0);
        return true;
      }
    }
  }
  return false;
}

/// Check if a value is effectively (zext i32 to i64).
/// If so, set Src to the original i32 value and return true.
/// Handles: ZERO_EXTEND, ANY_EXTEND, BUILD_PAIR(lo, 0), zero-extending loads.
static bool isZExtFromI32(SDValue V, SDValue &Src) {
  // Look through MERGE_VALUES which the type legalizer inserts.
  if (V.getOpcode() == ISD::MERGE_VALUES)
    return isZExtFromI32(V.getOperand(V.getResNo()), Src);

  if (V.getOpcode() == ISD::ZERO_EXTEND && V.getValueType() == MVT::i64 &&
      V.getOperand(0).getValueType() == MVT::i32) {
    Src = V.getOperand(0);
    return true;
  }
  if (V.getOpcode() == ISD::ANY_EXTEND && V.getValueType() == MVT::i64 &&
      V.getOperand(0).getValueType() == MVT::i32) {
    Src = V.getOperand(0);
    return true;
  }
  // Match BUILD_PAIR(Lo, Hi) where Hi is zero.
  // This is how the i8085 backend lowers zext i32 to i64.
  if (V.getOpcode() == ISD::BUILD_PAIR && V.getValueType() == MVT::i64) {
    SDValue Lo = V.getOperand(0);
    SDValue Hi = V.getOperand(1);
    if (Lo.getValueType() == MVT::i32 && Hi.getValueType() == MVT::i32) {
      if (auto *HiC = dyn_cast<ConstantSDNode>(Hi)) {
        if (HiC->isZero()) {
          Src = Lo;
          return true;
        }
      }
    }
  }
  return false;
}

/// Emit a library call taking two i32 args and returning RetVT (i32).
/// The callee is identified by the given symbol name.
static SDValue emitMul32LibCall(SelectionDAG &DAG, const SDLoc &DL,
                                const char *Name, EVT RetVT,
                                SDValue Arg0, SDValue Arg1,
                                const I8085TargetLowering &TLI) {
  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;

  Type *I32Ty = Type::getInt32Ty(*DAG.getContext());

  Entry.Node = Arg0;
  Entry.Ty = I32Ty;
  Args.push_back(Entry);

  Entry.Node = Arg1;
  Entry.Ty = I32Ty;
  Args.push_back(Entry);

  Type *RetTy = RetVT.getTypeForEVT(*DAG.getContext());
  auto PtrVT = TLI.getPointerTy(DAG.getDataLayout());
  SDValue Callee = DAG.getExternalSymbol(Name, PtrVT);

  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(DL)
      .setChain(DAG.getEntryNode())
      .setLibCallee(CallingConv::C, RetTy, Callee, std::move(Args));

  std::pair<SDValue, SDValue> CallResult = TLI.LowerCallTo(CLI);
  return CallResult.first;
}

/// Emit a library call for __mulsi32 which takes two i32 args and returns i64
/// via sret pointer (since i64 cannot be returned in registers on i8085).
static SDValue emitMul32I64LibCall(SelectionDAG &DAG, const SDLoc &DL,
                                   const char *Name,
                                   SDValue Arg0, SDValue Arg1,
                                   const I8085TargetLowering &TLI) {
  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;

  Type *I32Ty = Type::getInt32Ty(*DAG.getContext());
  Type *I64Ty = Type::getInt64Ty(*DAG.getContext());
  auto PtrVT = TLI.getPointerTy(DAG.getDataLayout());

  // Allocate stack space for the sret return value.
  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  int RetFI = MFI.CreateStackObject(8, Align(1), false);
  SDValue RetPtr = DAG.getFrameIndex(RetFI, PtrVT);

  // First arg: sret pointer
  TargetLowering::ArgListEntry RetEntry;
  RetEntry.Node = RetPtr;
  RetEntry.Ty = PointerType::getUnqual(I64Ty);
  RetEntry.IsSRet = true;
  RetEntry.IndirectType = I64Ty;
  Args.push_back(RetEntry);

  Entry.Node = Arg0;
  Entry.Ty = I32Ty;
  Args.push_back(Entry);

  Entry.Node = Arg1;
  Entry.Ty = I32Ty;
  Args.push_back(Entry);

  // Return type is void (result written to sret pointer).
  Type *VoidTy = Type::getVoidTy(*DAG.getContext());
  SDValue Callee = DAG.getExternalSymbol(Name, PtrVT);

  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(DL)
      .setChain(DAG.getEntryNode())
      .setLibCallee(CallingConv::C, VoidTy, Callee, std::move(Args));

  std::pair<SDValue, SDValue> CallInfo = TLI.LowerCallTo(CLI);

  // Load the i64 result from the sret pointer.
  SDValue Load = DAG.getLoad(MVT::i64, DL, CallInfo.second, RetPtr,
                             MachinePointerInfo());
  return Load;
}

// Like emitMul32I64LibCall, but returns {lo32, hi32} as separate i32 loads.
// This avoids creating i64 DAG nodes, which is critical during operation
// legalization when i64 has already been type-legalized away.
static std::pair<SDValue, SDValue>
emitMul32LoHi(SelectionDAG &DAG, const SDLoc &DL, const char *Name,
              SDValue Arg0, SDValue Arg1,
              const I8085TargetLowering &TLI) {
  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;

  Type *I32Ty = Type::getInt32Ty(*DAG.getContext());
  Type *I64Ty = Type::getInt64Ty(*DAG.getContext());
  auto PtrVT = TLI.getPointerTy(DAG.getDataLayout());

  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  int RetFI = MFI.CreateStackObject(8, Align(1), false);
  SDValue RetPtr = DAG.getFrameIndex(RetFI, PtrVT);

  TargetLowering::ArgListEntry RetEntry;
  RetEntry.Node = RetPtr;
  RetEntry.Ty = PointerType::getUnqual(I64Ty);
  RetEntry.IsSRet = true;
  RetEntry.IndirectType = I64Ty;
  Args.push_back(RetEntry);

  Entry.Node = Arg0;
  Entry.Ty = I32Ty;
  Args.push_back(Entry);

  Entry.Node = Arg1;
  Entry.Ty = I32Ty;
  Args.push_back(Entry);

  Type *VoidTy = Type::getVoidTy(*DAG.getContext());
  SDValue Callee = DAG.getExternalSymbol(Name, PtrVT);

  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(DL)
      .setChain(DAG.getEntryNode())
      .setLibCallee(CallingConv::C, VoidTy, Callee, std::move(Args));

  std::pair<SDValue, SDValue> CallInfo = TLI.LowerCallTo(CLI);
  SDValue Chain = CallInfo.second;

  // Load lo32 and hi32 separately (little-endian: lo at offset 0, hi at +4).
  SDValue Lo = DAG.getLoad(MVT::i32, DL, Chain, RetPtr,
                           MachinePointerInfo());
  SDValue HiPtr = DAG.getNode(ISD::ADD, DL, PtrVT, RetPtr,
                              DAG.getConstant(4, DL, PtrVT));
  SDValue Hi = DAG.getLoad(MVT::i32, DL, Lo.getValue(1), HiPtr,
                           MachinePointerInfo());
  return {Lo, Hi};
}

SDValue I8085TargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();

  auto lowerI64LibCall = [&](RTLIB::Libcall LC, SDValue LHS,
                             SDValue RHS) -> SDValue {
    ArgListTy Args;
    SDValue Chain = DAG.getEntryNode();
    auto PtrVT = getPointerTy(DAG.getDataLayout());
    Type *RetTy = VT.getTypeForEVT(*DAG.getContext());
    Type *RetTyABI = RetTy;

    // Use an sret temp to match clang's ABI for >32-bit integer returns.
    MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
    int RetFI = MFI.CreateStackObject(8, Align(1), false);
    SDValue RetPtr = DAG.getFrameIndex(RetFI, PtrVT);

    ArgListEntry RetEntry;
    RetEntry.Node = RetPtr;
    RetEntry.Ty = PointerType::getUnqual(RetTy);
    RetEntry.IsSRet = true;
    RetEntry.IndirectType = RetTy;
    Args.push_back(RetEntry);
    RetTyABI = Type::getVoidTy(*DAG.getContext());

    ArgListEntry A1;
    A1.Node = LHS;
    A1.Ty = LHS.getValueType().getTypeForEVT(*DAG.getContext());
    Args.push_back(A1);

    ArgListEntry A2;
    A2.Node = RHS;
    A2.Ty = RHS.getValueType().getTypeForEVT(*DAG.getContext());
    Args.push_back(A2);

    SDValue Callee = DAG.getExternalSymbol(getLibcallName(LC), PtrVT);
    TargetLowering::CallLoweringInfo CLI(DAG);
    CLI.setDebugLoc(DL)
        .setChain(Chain)
        .setLibCallee(getLibcallCallingConv(LC), RetTyABI, Callee,
                      std::move(Args));
    std::pair<SDValue, SDValue> CallInfo = LowerCallTo(CLI);

    SDValue Load =
        DAG.getLoad(VT, DL, CallInfo.second, RetPtr, MachinePointerInfo());
    return Load;
  };

  // Variant that takes a symbol name directly (for ops without RTLIB entries).
  auto lowerI64LibCallByName = [&](const char *Name, SDValue LHS,
                                    SDValue RHS) -> SDValue {
    ArgListTy Args;
    SDValue Chain = DAG.getEntryNode();
    auto PtrVT = getPointerTy(DAG.getDataLayout());
    Type *RetTy = Type::getInt64Ty(*DAG.getContext());

    MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
    int RetFI = MFI.CreateStackObject(8, Align(1), false);
    SDValue RetPtr = DAG.getFrameIndex(RetFI, PtrVT);

    ArgListEntry RetEntry;
    RetEntry.Node = RetPtr;
    RetEntry.Ty = PointerType::getUnqual(RetTy);
    RetEntry.IsSRet = true;
    RetEntry.IndirectType = RetTy;
    Args.push_back(RetEntry);

    ArgListEntry A1;
    A1.Node = LHS;
    A1.Ty = LHS.getValueType().getTypeForEVT(*DAG.getContext());
    Args.push_back(A1);

    ArgListEntry A2;
    A2.Node = RHS;
    A2.Ty = RHS.getValueType().getTypeForEVT(*DAG.getContext());
    Args.push_back(A2);

    SDValue Callee = DAG.getExternalSymbol(Name, PtrVT);
    TargetLowering::CallLoweringInfo CLI(DAG);
    CLI.setDebugLoc(DL)
        .setChain(Chain)
        .setLibCallee(CallingConv::C, Type::getVoidTy(*DAG.getContext()),
                      Callee, std::move(Args));
    std::pair<SDValue, SDValue> CallInfo = LowerCallTo(CLI);

    SDValue Load =
        DAG.getLoad(MVT::i64, DL, CallInfo.second, RetPtr,
                    MachinePointerInfo());
    return Load;
  };

  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("Don't know how to custom lower this!");
  case ISD::ATOMIC_FENCE:
  case ISD::ATOMIC_LOAD:
  case ISD::ATOMIC_STORE:
  case ISD::ATOMIC_CMP_SWAP:
  case ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS:
  case ISD::ATOMIC_SWAP:
  case ISD::ATOMIC_LOAD_ADD:
  case ISD::ATOMIC_LOAD_SUB:
  case ISD::ATOMIC_LOAD_AND:
  case ISD::ATOMIC_LOAD_CLR:
  case ISD::ATOMIC_LOAD_OR:
  case ISD::ATOMIC_LOAD_XOR:
  case ISD::ATOMIC_LOAD_NAND:
  case ISD::ATOMIC_LOAD_MIN:
  case ISD::ATOMIC_LOAD_MAX:
  case ISD::ATOMIC_LOAD_UMIN:
  case ISD::ATOMIC_LOAD_UMAX:
  case ISD::ATOMIC_LOAD_FADD:
  case ISD::ATOMIC_LOAD_FSUB:
  case ISD::ATOMIC_LOAD_FMAX:
  case ISD::ATOMIC_LOAD_FMIN:
  case ISD::ATOMIC_LOAD_UINC_WRAP:
  case ISD::ATOMIC_LOAD_UDEC_WRAP:
    fail(DL, DAG, "i8085 does not support atomic operations");
    return SDValue();
  case ISD::VASTART:
    return LowerVASTART(Op, DAG);
  case ISD::VAEND:
    return LowerVAEND(Op, DAG);
  case ISD::VACOPY:
    return LowerVACOPY(Op, DAG);
  case ISD::ADD:
    if (VT == MVT::i64)
      return lowerI64LibCallByName("__adddi3", Op.getOperand(0),
                                    Op.getOperand(1));
    break;
  case ISD::SUB:
    if (VT == MVT::i64)
      return lowerI64LibCallByName("__subdi3", Op.getOperand(0),
                                    Op.getOperand(1));
    break;
  case ISD::AND:
    if (VT == MVT::i64)
      return lowerI64LibCallByName("__anddi3", Op.getOperand(0),
                                    Op.getOperand(1));
    break;
  case ISD::OR:
    if (VT == MVT::i64)
      return lowerI64LibCallByName("__ordi3", Op.getOperand(0),
                                    Op.getOperand(1));
    break;
  case ISD::XOR:
    if (VT == MVT::i64)
      return lowerI64LibCallByName("__xordi3", Op.getOperand(0),
                                    Op.getOperand(1));
    break;
  case ISD::MUL:
    if (VT == MVT::i64) {
      // Check if both operands are sext from i32 -> use __mulsi32
      // which is a specialized signed 32x32->64 multiply.
      // Specialized shift+truncate patterns (__mulsi32_shr16, __mulsi32_hi32)
      // are matched in performTruncMulCombine during pre-legalize DAG combine.
      auto getSExtI32Operand = [&](SDValue V) -> SDValue {
        SDValue Src;
        if (isSExtFromI32(V, Src))
          return Src;
        // Also match sign-extending loads
        if (V.getOpcode() == ISD::LOAD && V.getValueType() == MVT::i64) {
          auto *LD = cast<LoadSDNode>(V.getNode());
          if (LD->getExtensionType() == ISD::SEXTLOAD &&
              LD->getMemoryVT() == MVT::i32) {
            return DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, V);
          }
        }
        // Fallback: use ComputeNumSignBits to detect sign extension.
        // This handles BUILD_PAIR patterns that may have been simplified
        // by the DAG combiner (e.g., after type legalization of sext i64).
        // If the i64 value has >= 33 sign bits, it's a sign-extended i32.
        if (V.getValueType() == MVT::i64 &&
            DAG.ComputeNumSignBits(V) >= 33) {
          return DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, V);
        }
        return SDValue();
      };
      SDValue A32 = getSExtI32Operand(Op.getOperand(0));
      SDValue B32 = getSExtI32Operand(Op.getOperand(1));
      if (A32 && B32) {
        return emitMul32I64LibCall(DAG, DL, "__mulsi32", A32, B32, *this);
      }
      // Check if both operands are zext from i32 -> use __mului32
      // which is a specialized unsigned 32x32->64 multiply (no sign handling).
      auto getZExtI32Operand = [&](SDValue V) -> SDValue {
        SDValue Src;
        if (isZExtFromI32(V, Src))
          return Src;
        // Also match zero-extending loads
        if (V.getOpcode() == ISD::LOAD && V.getValueType() == MVT::i64) {
          auto *LD = cast<LoadSDNode>(V.getNode());
          if (LD->getExtensionType() == ISD::ZEXTLOAD &&
              LD->getMemoryVT() == MVT::i32) {
            return DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, V);
          }
        }
        // Fallback: use computeKnownBits to detect zero extension.
        // If the upper 32 bits are all known zero, it's a zero-extended i32.
        if (V.getValueType() == MVT::i64) {
          KnownBits Known = DAG.computeKnownBits(V);
          if (Known.Zero.countLeadingOnes() >= 32) {
            return DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, V);
          }
        }
        return SDValue();
      };
      SDValue AU32 = getZExtI32Operand(Op.getOperand(0));
      SDValue BU32 = getZExtI32Operand(Op.getOperand(1));
      if (AU32 && BU32) {
        return emitMul32I64LibCall(DAG, DL, "__mului32", AU32, BU32, *this);
      }
      // Otherwise use the general __muldi3 library call
      return lowerI64LibCall(RTLIB::MUL_I64, Op.getOperand(0),
                             Op.getOperand(1));
    }
    if (VT == MVT::i32) {
      // Check if both operands are sext from i16 -> use __mulsi16
      // This handles SIGN_EXTEND, SIGN_EXTEND_INREG, and sext loads.
      auto getSExtI16Operand = [&](SDValue V) -> SDValue {
        SDValue Src;
        if (isSExtFromI16(V, Src))
          return Src;
        // Also match sign-extending loads
        if (V.getOpcode() == ISD::LOAD && V.getValueType() == MVT::i32) {
          auto *LD = cast<LoadSDNode>(V.getNode());
          if (LD->getExtensionType() == ISD::SEXTLOAD &&
              LD->getMemoryVT() == MVT::i16) {
            return DAG.getNode(ISD::TRUNCATE, DL, MVT::i16, V);
          }
        }
        return SDValue();
      };
      SDValue A = getSExtI16Operand(Op.getOperand(0));
      SDValue B = getSExtI16Operand(Op.getOperand(1));
      if (A && B) {
        return emitMul16LibCall(DAG, DL, "__mulsi16", MVT::i32, A, B, *this);
      }
      // Check if both operands are zext from i16 -> use __mului16
      // (unsigned widening 16x16->32 multiply, no sign handling)
      auto getZExtI16Operand = [&](SDValue V) -> SDValue {
        SDValue Src;
        if (isZExtFromI16(V, Src))
          return Src;
        // Also match zero-extending loads
        if (V.getOpcode() == ISD::LOAD && V.getValueType() == MVT::i32) {
          auto *LD = cast<LoadSDNode>(V.getNode());
          if (LD->getExtensionType() == ISD::ZEXTLOAD &&
              LD->getMemoryVT() == MVT::i16) {
            return DAG.getNode(ISD::TRUNCATE, DL, MVT::i16, V);
          }
        }
        return SDValue();
      };
      SDValue AU = getZExtI16Operand(Op.getOperand(0));
      SDValue BU = getZExtI16Operand(Op.getOperand(1));
      if (AU && BU) {
        return emitMul16LibCall(DAG, DL, "__mului16", MVT::i32, AU, BU, *this);
      }
      // Otherwise use the default __mul32 library call
      MakeLibCallOptions CallOptions;
      SDValue Result;
      SDValue Chain;
      std::tie(Result, Chain) = makeLibCall(DAG, RTLIB::MUL_I32, MVT::i32,
                                            {Op.getOperand(0), Op.getOperand(1)},
                                            CallOptions, DL);
      return Result;
    }
    if (VT == MVT::i16) {
      // Check if both operands are sext from i8 -> use __mulsi8
      // This handles SIGN_EXTEND, SIGN_EXTEND_INREG, and sext loads.
      auto getSExtI8Operand = [&](SDValue V) -> SDValue {
        SDValue Src;
        if (isSExtFromI8(V, Src))
          return Src;
        // Also match sign-extending loads
        if (V.getOpcode() == ISD::LOAD && V.getValueType() == MVT::i16) {
          auto *LD = cast<LoadSDNode>(V.getNode());
          if (LD->getExtensionType() == ISD::SEXTLOAD &&
              LD->getMemoryVT() == MVT::i8) {
            return DAG.getNode(ISD::TRUNCATE, DL, MVT::i8, V);
          }
        }
        return SDValue();
      };
      SDValue A = getSExtI8Operand(Op.getOperand(0));
      SDValue B = getSExtI8Operand(Op.getOperand(1));
      if (A && B) {
        return emitMul8LibCall(DAG, DL, "__mulsi8", MVT::i16, A, B, *this);
      }
      // Check if both operands are zext from i8 -> use __mului8
      // (unsigned widening 8x8->16 multiply, no sign handling)
      auto getZExtI8Operand = [&](SDValue V) -> SDValue {
        SDValue Src;
        if (isZExtFromI8(V, Src))
          return Src;
        // Also match zero-extending loads
        if (V.getOpcode() == ISD::LOAD && V.getValueType() == MVT::i16) {
          auto *LD = cast<LoadSDNode>(V.getNode());
          if (LD->getExtensionType() == ISD::ZEXTLOAD &&
              LD->getMemoryVT() == MVT::i8) {
            return DAG.getNode(ISD::TRUNCATE, DL, MVT::i8, V);
          }
        }
        return SDValue();
      };
      SDValue AU = getZExtI8Operand(Op.getOperand(0));
      SDValue BU = getZExtI8Operand(Op.getOperand(1));
      if (AU && BU) {
        return emitMul8LibCall(DAG, DL, "__mului8", MVT::i16, AU, BU, *this);
      }
      // Check for constant multiply strength reduction.
      // If one operand is a constant with a cheap shift-add decomposition,
      // emit an I8085ISD::MUL_IMM node instead of the library call.
      {
        SDValue MulLHS = Op.getOperand(0);
        SDValue MulRHS = Op.getOperand(1);
        const ConstantSDNode *MulC = dyn_cast<ConstantSDNode>(MulRHS);
        SDValue MulOther = MulLHS;
        if (!MulC) {
          MulC = dyn_cast<ConstantSDNode>(MulLHS);
          MulOther = MulRHS;
        }
        if (MulC) {
          uint64_t CVal64 = MulC->getZExtValue() & 0xFFFF;
          if (CVal64 >= 2 && CVal64 <= 0xFFFF && (CVal64 & (CVal64 - 1)) != 0) {
            uint16_t CVal = static_cast<uint16_t>(CVal64);
            // Check optimization level
            const Function &Fn = DAG.getMachineFunction().getFunction();
            bool IsMinSize = Fn.hasMinSize();
            bool IsOptNone = Fn.hasOptNone();
            if (!IsMinSize && !IsOptNone) {
              bool IsOptForSize = Fn.hasOptSize();
              // Cost estimation (same as ISel and pseudo expansion)
              auto estimateCost = [](uint16_t C) -> std::pair<unsigned, unsigned> {
                unsigned TZ = llvm::countr_zero(C);
                uint16_t Core = C >> TZ;
                unsigned BestB = UINT_MAX, BestC = UINT_MAX;
                auto tryB = [&](unsigned B, unsigned Cy) {
                  if (B < BestB || (B == BestB && Cy < BestC)) { BestB = B; BestC = Cy; }
                };
                if (Core >= 3 && ((Core-1)&(Core-2)) == 0) {
                  unsigned A = llvm::countr_zero((uint16_t)(Core-1));
                  tryB(A+3+TZ, A*10+18+TZ*10);
                }
                if (Core >= 3 && ((Core+1)&Core) == 0) {
                  unsigned A = llvm::countr_zero((unsigned)(Core+1));
                  tryB(A+8+TZ, A*10+32+TZ*10);
                }
                if (llvm::popcount(C) == 2) {
                  unsigned B = llvm::countr_zero(C);
                  unsigned A = 15 - llvm::countl_zero(C);
                  tryB(B+2+(A-B)+1, B*10+8+(A-B)*10+10);
                }
                {
                  uint16_t Lo = C & (-C);
                  uint16_t Sum = C + Lo;
                  if (Sum && (Sum & (Sum-1)) == 0) {
                    unsigned A = llvm::countr_zero(Sum);
                    unsigned B = llvm::countr_zero(Lo);
                    tryB(B+2+(A-B)+6, B*10+8+(A-B)*10+24);
                  }
                }
                if (Core > 1) {
                  for (unsigned A = 1; A <= 7; ++A) {
                    uint16_t F1 = (1u<<A)+1;
                    if (F1 > Core) break;
                    if (Core%F1 != 0) continue;
                    uint16_t F2 = Core/F1;
                    if (F2 <= 1) continue;
                    if (F2 >= 3 && ((F2-1)&(F2-2)) == 0) {
                      unsigned B = llvm::countr_zero((uint16_t)(F2-1));
                      tryB(A+3+B+3+TZ, A*10+18+B*10+18+TZ*10);
                    }
                    if ((F2 & (F2-1)) == 0) {
                      unsigned B = llvm::countr_zero(F2);
                      tryB(A+3+B+TZ, A*10+18+B*10+TZ*10);
                    }
                  }
                }
                {
                  uint16_t Cp = C+1;
                  if (Cp > 0) {
                    if ((Cp&(Cp-1)) == 0) {
                      unsigned N = llvm::countr_zero(Cp);
                      tryB(N+8, N*10+32);
                    } else {
                      unsigned TZp = llvm::countr_zero(Cp);
                      uint16_t Corep = Cp>>TZp;
                      if (Corep >= 3 && ((Corep-1)&(Corep-2)) == 0) {
                        unsigned A = llvm::countr_zero((uint16_t)(Corep-1));
                        tryB(A+3+TZp+8, A*10+18+TZp*10+32);
                      }
                    }
                  }
                }
                {
                  uint16_t Cm = C-1;
                  if (Cm > 1) {
                    if ((Cm&(Cm-1)) == 0) {
                      unsigned N = llvm::countr_zero(Cm);
                      tryB(N+3, N*10+18);
                    } else {
                      unsigned TZp = llvm::countr_zero(Cm);
                      uint16_t Corep = Cm>>TZp;
                      if (Corep >= 3 && ((Corep-1)&(Corep-2)) == 0) {
                        unsigned A = llvm::countr_zero((uint16_t)(Corep-1));
                        tryB(A+3+TZp+3, A*10+18+TZp*10+18);
                      }
                    }
                  }
                }
                return {BestB, BestC};
              };
              auto [CostBytes, CostCycles] = estimateCost(CVal);
              if (CostBytes != UINT_MAX) {
                const unsigned LibCallCycles = 170;
                // At -Os, inline if decomposition fits in 16 bytes.
                // The __mul16 library routine is ~58 bytes of ROM.
                // Even with 2 call sites sharing it, 2×16 = 32 bytes
                // inline < 58 + 2×8 = 74 bytes with library. Only
                // break even around 10+ call sites. Always inlining
                // also enables --gc-sections to strip __mul16 entirely.
                bool ShouldInline;
                if (IsOptForSize)
                  ShouldInline = (CostBytes <= 16);
                else
                  ShouldInline = (CostCycles < LibCallCycles) && (CostBytes <= 16);
                if (ShouldInline) {
                  return DAG.getNode(I8085ISD::MUL_IMM, DL, MVT::i16, MulOther,
                                     DAG.getTargetConstant(CVal, DL, MVT::i16));
                }
              }
            }
          }
        }
      }
      // Otherwise fall back to the default __mul16 library call
      MakeLibCallOptions CallOptions;
      SDValue Result;
      SDValue Chain;
      std::tie(Result, Chain) = makeLibCall(DAG, RTLIB::MUL_I16, MVT::i16,
                                            {Op.getOperand(0), Op.getOperand(1)},
                                            CallOptions, DL);
      return Result;
    }
    break;
  case ISD::SDIV:
    if (VT == MVT::i64)
      return lowerI64LibCall(RTLIB::SDIV_I64, Op.getOperand(0),
                             Op.getOperand(1));
    break;
  case ISD::UDIV:
    if (VT == MVT::i64)
      return lowerI64LibCall(RTLIB::UDIV_I64, Op.getOperand(0),
                             Op.getOperand(1));
    break;
  case ISD::SREM:
    if (VT == MVT::i64)
      return lowerI64LibCall(RTLIB::SREM_I64, Op.getOperand(0),
                             Op.getOperand(1));
    break;
  case ISD::UREM:
    if (VT == MVT::i64)
      return lowerI64LibCall(RTLIB::UREM_I64, Op.getOperand(0),
                             Op.getOperand(1));
    break;
  case ISD::SDIVREM:
  case ISD::UDIVREM:
    return LowerDivRem(Op, DAG);
  case ISD::ZERO_EXTEND:
  case ISD::ANY_EXTEND:
    if (VT == MVT::i64) {
      SDValue In = Op.getOperand(0);
      EVT InVT = In.getValueType();
      SDValue Lo = (InVT == MVT::i32)
                       ? In
                       : DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i32, In);
      SDValue Hi = DAG.getConstant(0, DL, MVT::i32);
      return buildI64Value(Lo, Hi, DAG, DL);
    }
    break;
  case ISD::SIGN_EXTEND:
    if (VT == MVT::i64) {
      SDValue In = Op.getOperand(0);
      EVT InVT = In.getValueType();
      SDValue Lo = (InVT == MVT::i32)
                       ? In
                       : DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i32, In);
      SDValue Zero = DAG.getConstant(0, DL, MVT::i32);
      SDValue NegOne = DAG.getConstant(-1, DL, MVT::i32);
      SDValue Sign = DAG.getSetCC(DL, MVT::i1, Lo, Zero, ISD::SETLT);
      SDValue Hi = DAG.getSelect(DL, MVT::i32, Sign, NegOne, Zero);
      return buildI64Value(Lo, Hi, DAG, DL);
    }
    break;
  case ISD::CTLZ:
  case ISD::CTLZ_ZERO_UNDEF:
    if (VT == MVT::i32) {
      MakeLibCallOptions CallOptions;
      SDValue Result;
      SDValue Chain;
      std::tie(Result, Chain) = makeLibCall(DAG, RTLIB::CTLZ_I32, VT,
                                            {Op.getOperand(0)}, CallOptions,
                                            DL);
      return Result;
    }
    if (VT == MVT::i64) {
      MakeLibCallOptions CallOptions;
      SDValue Result32;
      SDValue Chain;
      std::tie(Result32, Chain) =
          makeLibCall(DAG, RTLIB::CTLZ_I64, MVT::i32, {Op.getOperand(0)},
                      CallOptions, DL);
      return DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, Result32);
    }
    break;
  case ISD::CTTZ:
  case ISD::CTTZ_ZERO_UNDEF:
    if (VT == MVT::i32) {
      MakeLibCallOptions CallOptions;
      SDValue Result;
      SDValue Chain;
      std::tie(Result, Chain) = makeLibCall(DAG, RTLIB::CTTZ_I32, VT,
                                            {Op.getOperand(0)}, CallOptions,
                                            DL);
      return Result;
    }
    if (VT == MVT::i64) {
      MakeLibCallOptions CallOptions;
      SDValue Result32;
      SDValue Chain;
      std::tie(Result32, Chain) =
          makeLibCall(DAG, RTLIB::CTTZ_I64, MVT::i32, {Op.getOperand(0)},
                      CallOptions, DL);
      return DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, Result32);
    }
    break;
  // --- Tier 1: CTPOP ---
  case ISD::CTPOP: {
    if (VT == MVT::i32) {
      // Manual libcall to __popcountsi2 (no RTLIB enum for CTPOP)
      ArgListTy Args;
      auto PtrVT = getPointerTy(DAG.getDataLayout());
      Type *I32Ty = Type::getInt32Ty(*DAG.getContext());

      ArgListEntry A1;
      A1.Node = Op.getOperand(0);
      A1.Ty = I32Ty;
      Args.push_back(A1);

      SDValue Callee = DAG.getExternalSymbol("__popcountsi2", PtrVT);
      TargetLowering::CallLoweringInfo CLI(DAG);
      CLI.setDebugLoc(DL)
          .setChain(DAG.getEntryNode())
          .setLibCallee(CallingConv::C, I32Ty, Callee, std::move(Args));
      std::pair<SDValue, SDValue> CallInfo = LowerCallTo(CLI);
      return CallInfo.first;
    }
    if (VT == MVT::i64) {
      // Split into lo/hi i32, popcount each, add results, zero-extend.
      SDValue Lo, Hi;
      splitI64Value(Op.getOperand(0), Lo, Hi, DAG, DL);
      SDValue PopLo = DAG.getNode(ISD::CTPOP, DL, MVT::i32, Lo);
      SDValue PopHi = DAG.getNode(ISD::CTPOP, DL, MVT::i32, Hi);
      SDValue Sum = DAG.getNode(ISD::ADD, DL, MVT::i32, PopLo, PopHi);
      return DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, Sum);
    }
    break;
  }
  // --- Tier 2: BSWAP i64 ---
  case ISD::BSWAP: {
    if (VT == MVT::i64) {
      // Split into lo/hi i32, bswap each half, swap positions.
      // bswap(i64 x) = BUILD_PAIR(bswap(hi32), bswap(lo32))
      SDValue Lo, Hi;
      splitI64Value(Op.getOperand(0), Lo, Hi, DAG, DL);
      SDValue BswapLo = DAG.getNode(ISD::BSWAP, DL, MVT::i32, Lo);
      SDValue BswapHi = DAG.getNode(ISD::BSWAP, DL, MVT::i32, Hi);
      // Swap: new_lo = bswap(hi), new_hi = bswap(lo)
      return buildI64Value(BswapHi, BswapLo, DAG, DL);
    }
    break;
  }
  // --- Tier 3: ROTL/ROTR ---
  case ISD::ROTL:
  case ISD::ROTR: {
    SDValue Val = Op.getOperand(0);
    SDValue Amt = Op.getOperand(1);
    bool IsROTL = Op.getOpcode() == ISD::ROTL;

    if (VT == MVT::i8) {
      // DAG-level expansion: (x << n) | (x >> (8-n))
      EVT ShVT = MVT::i8;
      if (auto *CAmt = dyn_cast<ConstantSDNode>(Amt)) {
        unsigned ShAmt = CAmt->getZExtValue() % 8;
        if (ShAmt == 0)
          return Val;
        // Optimize: ROTL by N > 4 = ROTR by (8-N)
        if (IsROTL && ShAmt > 4) {
          ShAmt = 8 - ShAmt;
          IsROTL = false;
        } else if (!IsROTL && ShAmt > 4) {
          ShAmt = 8 - ShAmt;
          IsROTL = true;
        }
        SDValue ShlAmt = DAG.getConstant(IsROTL ? ShAmt : (8 - ShAmt), DL, ShVT);
        SDValue SrlAmt = DAG.getConstant(IsROTL ? (8 - ShAmt) : ShAmt, DL, ShVT);
        SDValue Shl = DAG.getNode(ISD::SHL, DL, VT, Val, ShlAmt);
        SDValue Srl = DAG.getNode(ISD::SRL, DL, VT, Val, SrlAmt);
        return DAG.getNode(ISD::OR, DL, VT, Shl, Srl);
      }
      // Variable amount
      SDValue WidthC = DAG.getConstant(8, DL, ShVT);
      SDValue Mask = DAG.getConstant(7, DL, ShVT);
      SDValue AmtMod = DAG.getNode(ISD::AND, DL, ShVT, Amt, Mask);
      SDValue InvAmt = DAG.getNode(ISD::SUB, DL, ShVT, WidthC, AmtMod);
      SDValue ShlAmt = IsROTL ? AmtMod : InvAmt;
      SDValue SrlAmt = IsROTL ? InvAmt : AmtMod;
      SDValue Shl = DAG.getNode(ISD::SHL, DL, VT, Val, ShlAmt);
      SDValue Srl = DAG.getNode(ISD::SRL, DL, VT, Val, SrlAmt);
      return DAG.getNode(ISD::OR, DL, VT, Shl, Srl);
    }
    if (VT == MVT::i16) {
      // Constant amounts use shift+or at DAG level
      if (auto *CAmt = dyn_cast<ConstantSDNode>(Amt)) {
        unsigned ShAmt = CAmt->getZExtValue() % 16;
        if (ShAmt == 0) return Val;
        EVT ShVT = MVT::i8;
        SDValue ShlAmt = DAG.getConstant(IsROTL ? ShAmt : (16 - ShAmt), DL, ShVT);
        SDValue SrlAmt = DAG.getConstant(IsROTL ? (16 - ShAmt) : ShAmt, DL, ShVT);
        SDValue Shl = DAG.getNode(ISD::SHL, DL, VT, Val, ShlAmt);
        SDValue Srl = DAG.getNode(ISD::SRL, DL, VT, Val, SrlAmt);
        return DAG.getNode(ISD::OR, DL, VT, Shl, Srl);
      }
      // Variable: emit libcall
      const char *Name = IsROTL ? "__rotlhi2" : "__rotrhi2";
      ArgListTy Args;
      auto PtrVT = getPointerTy(DAG.getDataLayout());
      Type *I16Ty = Type::getInt16Ty(*DAG.getContext());
      Type *I8Ty = Type::getInt8Ty(*DAG.getContext());
      ArgListEntry A1;
      A1.Node = Val;
      A1.Ty = I16Ty;
      Args.push_back(A1);
      ArgListEntry A2;
      SDValue AmtTrunc = DAG.getNode(ISD::TRUNCATE, DL, MVT::i8, Amt);
      A2.Node = AmtTrunc;
      A2.Ty = I8Ty;
      Args.push_back(A2);
      SDValue Callee = DAG.getExternalSymbol(Name, PtrVT);
      TargetLowering::CallLoweringInfo CLI(DAG);
      CLI.setDebugLoc(DL)
          .setChain(DAG.getEntryNode())
          .setLibCallee(CallingConv::C, I16Ty, Callee, std::move(Args));
      std::pair<SDValue, SDValue> CallInfo = LowerCallTo(CLI);
      return CallInfo.first;
    }
    if (VT == MVT::i32) {
      // Constant amounts use shift+or at DAG level
      if (auto *CAmt = dyn_cast<ConstantSDNode>(Amt)) {
        unsigned ShAmt = CAmt->getZExtValue() % 32;
        if (ShAmt == 0) return Val;
        EVT ShVT = MVT::i8;
        SDValue ShlAmt = DAG.getConstant(IsROTL ? ShAmt : (32 - ShAmt), DL, ShVT);
        SDValue SrlAmt = DAG.getConstant(IsROTL ? (32 - ShAmt) : ShAmt, DL, ShVT);
        SDValue Shl = DAG.getNode(ISD::SHL, DL, VT, Val, ShlAmt);
        SDValue Srl = DAG.getNode(ISD::SRL, DL, VT, Val, SrlAmt);
        return DAG.getNode(ISD::OR, DL, VT, Shl, Srl);
      }
      // Variable: emit libcall
      const char *Name = IsROTL ? "__rotlsi2" : "__rotrsi2";
      ArgListTy Args;
      auto PtrVT = getPointerTy(DAG.getDataLayout());
      Type *I32Ty = Type::getInt32Ty(*DAG.getContext());
      Type *I8Ty = Type::getInt8Ty(*DAG.getContext());
      ArgListEntry A1;
      A1.Node = Val;
      A1.Ty = I32Ty;
      Args.push_back(A1);
      ArgListEntry A2;
      SDValue AmtTrunc = DAG.getNode(ISD::TRUNCATE, DL, MVT::i8, Amt);
      A2.Node = AmtTrunc;
      A2.Ty = I8Ty;
      Args.push_back(A2);
      SDValue Callee = DAG.getExternalSymbol(Name, PtrVT);
      TargetLowering::CallLoweringInfo CLI(DAG);
      CLI.setDebugLoc(DL)
          .setChain(DAG.getEntryNode())
          .setLibCallee(CallingConv::C, I32Ty, Callee, std::move(Args));
      std::pair<SDValue, SDValue> CallInfo = LowerCallTo(CLI);
      return CallInfo.first;
    }
    if (VT == MVT::i64) {
      // Constant amounts use shift+or at DAG level (via i64 shift libcalls)
      if (auto *CAmt = dyn_cast<ConstantSDNode>(Amt)) {
        unsigned ShAmt = CAmt->getZExtValue() % 64;
        if (ShAmt == 0) return Val;
        SDValue ShlAmt = DAG.getConstant(IsROTL ? ShAmt : (64 - ShAmt), DL, MVT::i32);
        SDValue SrlAmt = DAG.getConstant(IsROTL ? (64 - ShAmt) : ShAmt, DL, MVT::i32);
        SDValue Shl = DAG.getNode(ISD::SHL, DL, VT, Val, ShlAmt);
        SDValue Srl = DAG.getNode(ISD::SRL, DL, VT, Val, SrlAmt);
        return DAG.getNode(ISD::OR, DL, VT, Shl, Srl);
      }
      // Variable: emit sret libcall
      const char *Name = IsROTL ? "__rotldi2" : "__rotrdi2";
      SDValue AmtI32 = Amt;
      if (Amt.getValueType() != MVT::i32)
        AmtI32 = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i32, Amt);
      return lowerI64LibCallByName(Name, Val, AmtI32);
    }
    break;
  }
  case ISD::LOAD:
    if (auto Res = lowerI64Load(Op, DAG))
      return Res;
    break;
  case ISD::STORE:
    if (auto Res = lowerI64Store(Op, DAG))
      return Res;
    break;
  case ISD::SHL:
  case ISD::SRL:
  case ISD::SRA:
    if (VT == MVT::i64) {
      RTLIB::Libcall LC;
      switch (Op.getOpcode()) {
      case ISD::SHL: LC = RTLIB::SHL_I64; break;
      case ISD::SRL: LC = RTLIB::SRL_I64; break;
      case ISD::SRA: LC = RTLIB::SRA_I64; break;
      default: llvm_unreachable("Unexpected shift opcode");
      }
      return lowerI64LibCall(LC, Op.getOperand(0), Op.getOperand(1));
    }
    if (VT == MVT::i32) {
      // Hybrid approach: constant shifts are handled inline by ISel
      // (byte-shuffle + rotates), variable shifts use library calls.
      SDValue RHS = Op.getOperand(1);
      if (isa<ConstantSDNode>(RHS))
        return Op; // Identity: let ISel handle with byte-shuffle + rotates.

      // Variable shift: emit library call.
      RTLIB::Libcall LC;
      switch (Op.getOpcode()) {
      case ISD::SHL: LC = RTLIB::SHL_I32; break;
      case ISD::SRL: LC = RTLIB::SRL_I32; break;
      case ISD::SRA: LC = RTLIB::SRA_I32; break;
      default: llvm_unreachable("Unexpected shift opcode");
      }
      // Extend shift amount to i32 to match __ashlsi3(uint32_t, uint32_t).
      SDValue LHS = Op.getOperand(0);
      SDValue Amt = DAG.getZExtOrTrunc(RHS, DL, MVT::i32);
      SDValue Ops[] = {LHS, Amt};
      TargetLowering::MakeLibCallOptions CallOptions;
      return makeLibCall(DAG, LC, MVT::i32, Ops, CallOptions, DL).first;
    }
    break;
  case ISD::SIGN_EXTEND_INREG: {
    SDValue Val = Op.getOperand(0);
    EVT FromVT = cast<VTSDNode>(Op.getOperand(1))->getVT();
    unsigned FromBits = FromVT.getScalarSizeInBits();
    unsigned ToBits = VT.getScalarSizeInBits();
    if (FromBits >= ToBits)
      return Val;
    SDValue ShiftAmt = DAG.getConstant(ToBits - FromBits, DL, VT);
    SDValue Shl = DAG.getNode(ISD::SHL, DL, VT, Val, ShiftAmt);
    return DAG.getNode(ISD::SRA, DL, VT, Shl, ShiftAmt);
  }
  case ISD::SELECT_CC: {
    SDValue LHS = Op.getOperand(0);
    SDValue RHS = Op.getOperand(1);
    SDValue TrueV = Op.getOperand(2);
    SDValue FalseV = Op.getOperand(3);
    SDValue CC = Op.getOperand(4);
    EVT CmpVT = LHS.getValueType();
    EVT CCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), CmpVT);
    SDValue Cond = DAG.getNode(ISD::SETCC, DL, CCVT, LHS, RHS, CC, Op->getFlags());
    return DAG.getSelect(DL, VT, Cond, TrueV, FalseV, Op->getFlags());
  }
  case ISD::ABS: {
    SDValue N0 = Op.getOperand(0);
    SDValue Zero = DAG.getConstant(0, DL, VT);
    SDValue Neg = DAG.getNode(ISD::SUB, DL, VT, Zero, N0);
    EVT CCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
    SDValue Cond = DAG.getSetCC(DL, CCVT, N0, Zero, ISD::SETLT);
    return DAG.getSelect(DL, VT, Cond, Neg, N0);
  }
  // --- Tier 3: FSHL/FSHR for all widths ---
  case ISD::FSHL:
  case ISD::FSHR: {
    SDValue Hi = Op.getOperand(0);
    SDValue Lo = Op.getOperand(1);
    SDValue Amt = Op.getOperand(2);
    unsigned Width = VT.getSizeInBits();
    bool IsFSHL = Op.getOpcode() == ISD::FSHL;

    // i8 and i16: expand at DAG level (same as before)
    if (VT == MVT::i8 || VT == MVT::i16) {
      if (auto *CAmt = dyn_cast<ConstantSDNode>(Amt)) {
        unsigned ShAmt = CAmt->getZExtValue() % Width;
        if (ShAmt == 0)
          return IsFSHL ? Hi : Lo;

        if (VT == MVT::i16 && ShAmt == 8) {
          SDValue HiPart = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i8,
                                       IsFSHL ? Hi : Lo,
                                       DAG.getIntPtrConstant(1, DL));
          SDValue LoPart = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i8,
                                       IsFSHL ? Lo : Hi,
                                       DAG.getIntPtrConstant(0, DL));
          SDValue HiExt = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i16, HiPart);
          SDValue LoExt = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i16, LoPart);
          SDValue HiShift = DAG.getNode(ISD::SHL, DL, MVT::i16, HiExt,
                                        DAG.getConstant(8, DL, MVT::i8));
          return DAG.getNode(ISD::OR, DL, MVT::i16, HiShift, LoExt);
        }

        if (ShAmt == 1) {
          SDValue ShAmtLo = DAG.getConstant(1, DL, MVT::i8);
          SDValue ShAmtHi = DAG.getConstant(Width - 1, DL, MVT::i8);
          if (IsFSHL) {
            SDValue Shl = DAG.getNode(ISD::SHL, DL, VT, Hi, ShAmtLo);
            SDValue Srl = DAG.getNode(ISD::SRL, DL, VT, Lo, ShAmtHi);
            return DAG.getNode(ISD::OR, DL, VT, Shl, Srl);
          } else {
            SDValue Shl = DAG.getNode(ISD::SHL, DL, VT, Hi, ShAmtHi);
            SDValue Srl = DAG.getNode(ISD::SRL, DL, VT, Lo, ShAmtLo);
            return DAG.getNode(ISD::OR, DL, VT, Shl, Srl);
          }
        }
      }

      EVT ShVT = getShiftAmountTy(VT, DAG.getDataLayout());
      SDValue WidthConst = DAG.getConstant(Width, DL, ShVT);
      SDValue AmtMod = DAG.getNode(ISD::AND, DL, ShVT, Amt,
                                   DAG.getConstant(Width - 1, DL, ShVT));
      SDValue InvAmt = DAG.getNode(ISD::SUB, DL, ShVT, WidthConst, AmtMod);

      if (IsFSHL) {
        SDValue Shl = DAG.getNode(ISD::SHL, DL, VT, Hi, AmtMod);
        SDValue Srl = DAG.getNode(ISD::SRL, DL, VT, Lo, InvAmt);
        return DAG.getNode(ISD::OR, DL, VT, Shl, Srl);
      } else {
        SDValue Shl = DAG.getNode(ISD::SHL, DL, VT, Hi, InvAmt);
        SDValue Srl = DAG.getNode(ISD::SRL, DL, VT, Lo, AmtMod);
        return DAG.getNode(ISD::OR, DL, VT, Shl, Srl);
      }
    }

    // i32: constant amounts expand at DAG level, variable amounts use libcall
    if (VT == MVT::i32) {
      if (auto *CAmt = dyn_cast<ConstantSDNode>(Amt)) {
        unsigned ShAmt = CAmt->getZExtValue() % 32;
        if (ShAmt == 0) return IsFSHL ? Hi : Lo;
        EVT ShVT = MVT::i8;
        SDValue ShlAmt, SrlAmt;
        if (IsFSHL) {
          ShlAmt = DAG.getConstant(ShAmt, DL, ShVT);
          SrlAmt = DAG.getConstant(32 - ShAmt, DL, ShVT);
        } else {
          ShlAmt = DAG.getConstant(32 - ShAmt, DL, ShVT);
          SrlAmt = DAG.getConstant(ShAmt, DL, ShVT);
        }
        SDValue Shl = DAG.getNode(ISD::SHL, DL, VT, Hi, ShlAmt);
        SDValue Srl = DAG.getNode(ISD::SRL, DL, VT, Lo, SrlAmt);
        return DAG.getNode(ISD::OR, DL, VT, Shl, Srl);
      }
      // Variable: emit libcall to __fshlsi3 / __fshrsi3
      const char *Name = IsFSHL ? "__fshlsi3" : "__fshrsi3";
      ArgListTy Args;
      auto PtrVT = getPointerTy(DAG.getDataLayout());
      Type *I32Ty = Type::getInt32Ty(*DAG.getContext());
      Type *I8Ty = Type::getInt8Ty(*DAG.getContext());

      ArgListEntry AH;
      AH.Node = Hi;
      AH.Ty = I32Ty;
      Args.push_back(AH);

      ArgListEntry AL;
      AL.Node = Lo;
      AL.Ty = I32Ty;
      Args.push_back(AL);

      ArgListEntry AN;
      SDValue AmtTrunc = DAG.getNode(ISD::TRUNCATE, DL, MVT::i8, Amt);
      AN.Node = AmtTrunc;
      AN.Ty = I8Ty;
      Args.push_back(AN);

      SDValue Callee = DAG.getExternalSymbol(Name, PtrVT);
      TargetLowering::CallLoweringInfo CLI(DAG);
      CLI.setDebugLoc(DL)
          .setChain(DAG.getEntryNode())
          .setLibCallee(CallingConv::C, I32Ty, Callee, std::move(Args));
      std::pair<SDValue, SDValue> CallInfo = LowerCallTo(CLI);
      return CallInfo.first;
    }

    // i64: expand to (hi << n) | (lo >> (64-n)) using existing shift libcalls
    if (VT == MVT::i64) {
      if (auto *CAmt = dyn_cast<ConstantSDNode>(Amt)) {
        unsigned ShAmt = CAmt->getZExtValue() % 64;
        if (ShAmt == 0) return IsFSHL ? Hi : Lo;
        SDValue ShlAmt, SrlAmt;
        if (IsFSHL) {
          ShlAmt = DAG.getConstant(ShAmt, DL, MVT::i32);
          SrlAmt = DAG.getConstant(64 - ShAmt, DL, MVT::i32);
        } else {
          ShlAmt = DAG.getConstant(64 - ShAmt, DL, MVT::i32);
          SrlAmt = DAG.getConstant(ShAmt, DL, MVT::i32);
        }
        SDValue Shl = DAG.getNode(ISD::SHL, DL, VT, Hi, ShlAmt);
        SDValue Srl = DAG.getNode(ISD::SRL, DL, VT, Lo, SrlAmt);
        return DAG.getNode(ISD::OR, DL, VT, Shl, Srl);
      }
      // Variable: expand to (hi << n) | (lo >> (64-n))
      SDValue AmtI32 = Amt;
      if (Amt.getValueType() != MVT::i32)
        AmtI32 = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i32, Amt);
      SDValue C64 = DAG.getConstant(64, DL, MVT::i32);
      SDValue Mask63 = DAG.getConstant(63, DL, MVT::i32);
      SDValue AmtMod = DAG.getNode(ISD::AND, DL, MVT::i32, AmtI32, Mask63);
      SDValue InvAmt = DAG.getNode(ISD::SUB, DL, MVT::i32, C64, AmtMod);
      SDValue ShlAmt = IsFSHL ? AmtMod : InvAmt;
      SDValue SrlAmt = IsFSHL ? InvAmt : AmtMod;
      SDValue Shl = DAG.getNode(ISD::SHL, DL, VT, Hi, ShlAmt);
      SDValue Srl = DAG.getNode(ISD::SRL, DL, VT, Lo, SrlAmt);
      return DAG.getNode(ISD::OR, DL, VT, Shl, Srl);
    }
    break;
  }
  // --- Tier 4: Saturating Arithmetic ---
  case ISD::UADDSAT: {
    SDValue A = Op.getOperand(0);
    SDValue B = Op.getOperand(1);
    SDValue Sum = DAG.getNode(ISD::ADD, DL, VT, A, B);
    EVT CCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
    // Overflow when sum < a (unsigned)
    SDValue Overflow = DAG.getSetCC(DL, CCVT, Sum, A, ISD::SETULT);
    SDValue AllOnes = DAG.getConstant(APInt::getAllOnes(VT.getSizeInBits()), DL, VT);
    return DAG.getSelect(DL, VT, Overflow, AllOnes, Sum);
  }
  case ISD::SADDSAT: {
    SDValue A = Op.getOperand(0);
    SDValue B = Op.getOperand(1);
    SDValue Sum = DAG.getNode(ISD::ADD, DL, VT, A, B);
    unsigned Bits = VT.getSizeInBits();
    EVT CCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
    SDValue Zero = DAG.getConstant(0, DL, VT);

    // Signed overflow: (a ^ sum) & (b ^ sum) has sign bit set
    SDValue XorAS = DAG.getNode(ISD::XOR, DL, VT, A, Sum);
    SDValue XorBS = DAG.getNode(ISD::XOR, DL, VT, B, Sum);
    SDValue OvfTest = DAG.getNode(ISD::AND, DL, VT, XorAS, XorBS);
    SDValue OvfCond = DAG.getSetCC(DL, CCVT, OvfTest, Zero, ISD::SETLT);

    // Clamp: if a >= 0, INT_MAX, else INT_MIN
    SDValue SignA = DAG.getSetCC(DL, CCVT, A, Zero, ISD::SETLT);
    SDValue IntMax = DAG.getConstant(APInt::getSignedMaxValue(Bits), DL, VT);
    SDValue IntMin = DAG.getConstant(APInt::getSignedMinValue(Bits), DL, VT);
    SDValue Clamp = DAG.getSelect(DL, VT, SignA, IntMin, IntMax);

    return DAG.getSelect(DL, VT, OvfCond, Clamp, Sum);
  }
  case ISD::USUBSAT: {
    SDValue A = Op.getOperand(0);
    SDValue B = Op.getOperand(1);
    SDValue Diff = DAG.getNode(ISD::SUB, DL, VT, A, B);
    EVT CCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
    // Underflow when a < b (unsigned)
    SDValue Underflow = DAG.getSetCC(DL, CCVT, A, B, ISD::SETULT);
    SDValue Zero = DAG.getConstant(0, DL, VT);
    return DAG.getSelect(DL, VT, Underflow, Zero, Diff);
  }
  case ISD::SSUBSAT: {
    SDValue A = Op.getOperand(0);
    SDValue B = Op.getOperand(1);
    SDValue Diff = DAG.getNode(ISD::SUB, DL, VT, A, B);
    unsigned Bits = VT.getSizeInBits();
    EVT CCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
    SDValue Zero = DAG.getConstant(0, DL, VT);

    // Signed overflow: (a ^ b) & (a ^ diff) has sign bit set
    SDValue XorAB = DAG.getNode(ISD::XOR, DL, VT, A, B);
    SDValue XorAD = DAG.getNode(ISD::XOR, DL, VT, A, Diff);
    SDValue OvfTest = DAG.getNode(ISD::AND, DL, VT, XorAB, XorAD);
    SDValue OvfCond = DAG.getSetCC(DL, CCVT, OvfTest, Zero, ISD::SETLT);

    SDValue SignA = DAG.getSetCC(DL, CCVT, A, Zero, ISD::SETLT);
    SDValue IntMax = DAG.getConstant(APInt::getSignedMaxValue(Bits), DL, VT);
    SDValue IntMin = DAG.getConstant(APInt::getSignedMinValue(Bits), DL, VT);
    SDValue Clamp = DAG.getSelect(DL, VT, SignA, IntMin, IntMax);

    return DAG.getSelect(DL, VT, OvfCond, Clamp, Diff);
  }
  // --- Tier 5: Overflow Detection ---
  case ISD::UADDO: {
    SDValue A = Op.getOperand(0);
    SDValue B = Op.getOperand(1);
    SDValue Sum = DAG.getNode(ISD::ADD, DL, VT, A, B);
    EVT CCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
    SDValue Overflow = DAG.getSetCC(DL, CCVT, Sum, A, ISD::SETULT);
    return DAG.getMergeValues({Sum, Overflow}, DL);
  }
  case ISD::USUBO: {
    SDValue A = Op.getOperand(0);
    SDValue B = Op.getOperand(1);
    SDValue Diff = DAG.getNode(ISD::SUB, DL, VT, A, B);
    EVT CCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
    SDValue Overflow = DAG.getSetCC(DL, CCVT, A, B, ISD::SETULT);
    return DAG.getMergeValues({Diff, Overflow}, DL);
  }
  case ISD::SADDO: {
    SDValue A = Op.getOperand(0);
    SDValue B = Op.getOperand(1);
    SDValue Sum = DAG.getNode(ISD::ADD, DL, VT, A, B);
    EVT CCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
    SDValue Zero = DAG.getConstant(0, DL, VT);
    SDValue XorAS = DAG.getNode(ISD::XOR, DL, VT, A, Sum);
    SDValue XorBS = DAG.getNode(ISD::XOR, DL, VT, B, Sum);
    SDValue OvfTest = DAG.getNode(ISD::AND, DL, VT, XorAS, XorBS);
    SDValue Overflow = DAG.getSetCC(DL, CCVT, OvfTest, Zero, ISD::SETLT);
    return DAG.getMergeValues({Sum, Overflow}, DL);
  }
  case ISD::SSUBO: {
    SDValue A = Op.getOperand(0);
    SDValue B = Op.getOperand(1);
    SDValue Diff = DAG.getNode(ISD::SUB, DL, VT, A, B);
    EVT CCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
    SDValue Zero = DAG.getConstant(0, DL, VT);
    SDValue XorAB = DAG.getNode(ISD::XOR, DL, VT, A, B);
    SDValue XorAD = DAG.getNode(ISD::XOR, DL, VT, A, Diff);
    SDValue OvfTest = DAG.getNode(ISD::AND, DL, VT, XorAB, XorAD);
    SDValue Overflow = DAG.getSetCC(DL, CCVT, OvfTest, Zero, ISD::SETLT);
    return DAG.getMergeValues({Diff, Overflow}, DL);
  }
  case ISD::UMULO: {
    SDValue A = Op.getOperand(0);
    SDValue B = Op.getOperand(1);
    EVT CCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
    // Widen to next-wider unsigned multiply, check if high half != 0
    if (VT == MVT::i8) {
      SDValue AExt = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i16, A);
      SDValue BExt = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i16, B);
      SDValue Wide = DAG.getNode(ISD::MUL, DL, MVT::i16, AExt, BExt);
      SDValue Res = DAG.getNode(ISD::TRUNCATE, DL, MVT::i8, Wide);
      SDValue Hi = DAG.getNode(ISD::SRL, DL, MVT::i16, Wide,
                               DAG.getConstant(8, DL, MVT::i8));
      SDValue HiTrunc = DAG.getNode(ISD::TRUNCATE, DL, MVT::i8, Hi);
      SDValue Zero = DAG.getConstant(0, DL, MVT::i8);
      SDValue Overflow = DAG.getSetCC(DL, CCVT, HiTrunc, Zero, ISD::SETNE);
      return DAG.getMergeValues({Res, Overflow}, DL);
    }
    if (VT == MVT::i16) {
      SDValue AExt = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i32, A);
      SDValue BExt = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i32, B);
      SDValue Wide = DAG.getNode(ISD::MUL, DL, MVT::i32, AExt, BExt);
      SDValue Res = DAG.getNode(ISD::TRUNCATE, DL, MVT::i16, Wide);
      SDValue Hi = DAG.getNode(ISD::SRL, DL, MVT::i32, Wide,
                               DAG.getConstant(16, DL, MVT::i8));
      SDValue HiTrunc = DAG.getNode(ISD::TRUNCATE, DL, MVT::i16, Hi);
      SDValue Zero = DAG.getConstant(0, DL, MVT::i16);
      SDValue Overflow = DAG.getSetCC(DL, CCVT, HiTrunc, Zero, ISD::SETNE);
      return DAG.getMergeValues({Res, Overflow}, DL);
    }
    if (VT == MVT::i32) {
      // Use __mului32 (32x32->64 unsigned multiply) via sret, loading
      // the result as two i32 halves to avoid creating i64 DAG nodes
      // during operation legalization.
      auto [Lo, Hi] = emitMul32LoHi(DAG, DL, "__mului32", A, B, *this);
      SDValue Zero = DAG.getConstant(0, DL, MVT::i32);
      SDValue Overflow = DAG.getSetCC(DL, CCVT, Hi, Zero, ISD::SETNE);
      return DAG.getMergeValues({Lo, Overflow}, DL);
    }
    break;
  }
  case ISD::SMULO: {
    SDValue A = Op.getOperand(0);
    SDValue B = Op.getOperand(1);
    EVT CCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
    // Widen to next-wider signed multiply, check if high half != sign-extension
    if (VT == MVT::i8) {
      SDValue AExt = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i16, A);
      SDValue BExt = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i16, B);
      SDValue Wide = DAG.getNode(ISD::MUL, DL, MVT::i16, AExt, BExt);
      SDValue Res = DAG.getNode(ISD::TRUNCATE, DL, MVT::i8, Wide);
      // Sign-extend result back to i16
      SDValue ResSext = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i16, Res);
      // Overflow if wide != sext(trunc(wide))
      SDValue Overflow = DAG.getSetCC(DL, CCVT,
          DAG.getNode(ISD::TRUNCATE, DL, MVT::i8, Wide),
          DAG.getNode(ISD::TRUNCATE, DL, MVT::i8, Wide),
          ISD::SETEQ);
      // Actually: overflow = (wide != sext(lo))
      Overflow = DAG.getSetCC(DL, CCVT, Wide, ResSext, ISD::SETNE);
      return DAG.getMergeValues({Res, Overflow}, DL);
    }
    if (VT == MVT::i16) {
      SDValue AExt = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i32, A);
      SDValue BExt = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i32, B);
      SDValue Wide = DAG.getNode(ISD::MUL, DL, MVT::i32, AExt, BExt);
      SDValue Res = DAG.getNode(ISD::TRUNCATE, DL, MVT::i16, Wide);
      SDValue ResSext = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i32, Res);
      SDValue Overflow = DAG.getSetCC(DL, CCVT, Wide, ResSext, ISD::SETNE);
      return DAG.getMergeValues({Res, Overflow}, DL);
    }
    if (VT == MVT::i32) {
      // Use __mulsi32 (32x32->64 signed multiply) via sret, loading
      // the result as two i32 halves to avoid creating i64 DAG nodes.
      auto [Lo, Hi] = emitMul32LoHi(DAG, DL, "__mulsi32", A, B, *this);
      // Overflow if hi32 != sign-extension of lo32 (i.e. hi != lo >> 31).
      SDValue SignExt = DAG.getNode(ISD::SRA, DL, MVT::i32, Lo,
                                    DAG.getConstant(31, DL, MVT::i8));
      SDValue Overflow = DAG.getSetCC(DL, CCVT, Hi, SignExt, ISD::SETNE);
      return DAG.getMergeValues({Lo, Overflow}, DL);
    }
    break;
  }
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::BlockAddress:
    return LowerBlockAddress(Op, DAG);
  case ISD::ConstantPool:
    return LowerConstantPool(Op, DAG);
  case ISD::JumpTable:
    return LowerJumpTable(Op, DAG);
  }

  return SDValue();
}

SDValue I8085TargetLowering::LowerVASTART(SDValue Op,
                                       SelectionDAG &DAG) const {
  const MachineFunction &MF = DAG.getMachineFunction();
  const I8085MachineFunctionInfo *AFI = MF.getInfo<I8085MachineFunctionInfo>();
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  auto DL = DAG.getDataLayout();
  SDLoc dl(Op);

  // Store the address of the first vararg argument into the va_list.
  SDValue FI =
      DAG.getFrameIndex(AFI->getVarArgsFrameIndex(), getPointerTy(DL));

  return DAG.getStore(Op.getOperand(0), dl, FI, Op.getOperand(1),
                      MachinePointerInfo(SV));
}

SDValue I8085TargetLowering::LowerVAEND(SDValue Op, SelectionDAG &DAG) const {
  return Op.getOperand(0);
}

SDValue I8085TargetLowering::LowerVACOPY(SDValue Op, SelectionDAG &DAG) const {
  // va_list is a simple pointer on i8085, so va_copy is just a pointer copy.
  SDLoc dl(Op);
  auto DL = DAG.getDataLayout();
  EVT PtrVT = getPointerTy(DL);

  SDValue Chain = Op.getOperand(0);
  SDValue DstPtr = Op.getOperand(1);
  SDValue SrcPtr = Op.getOperand(2);
  const Value *DstSV = cast<SrcValueSDNode>(Op.getOperand(3))->getValue();
  const Value *SrcSV = cast<SrcValueSDNode>(Op.getOperand(4))->getValue();

  // Load the va_list pointer from the source.
  SDValue SrcVal =
      DAG.getLoad(PtrVT, dl, Chain, SrcPtr, MachinePointerInfo(SrcSV));
  // Store it to the destination.
  return DAG.getStore(SrcVal.getValue(1), dl, SrcVal, DstPtr,
                      MachinePointerInfo(DstSV));
}

/// Replace a node with an illegal result type
/// with a new node built out of custom code.
void I8085TargetLowering::ReplaceNodeResults(SDNode *N,
                                           SmallVectorImpl<SDValue> &Results,
                                           SelectionDAG &DAG) const {
  SDLoc DL(N);

  switch (N->getOpcode()) {
  case ISD::ADD: {
    // For i64, emit a libcall to __adddi3.
    if (N->getValueType(0) == MVT::i64) {
      SDValue Res = LowerOperation(SDValue(N, 0), DAG);
      for (unsigned I = 0, E = Res->getNumValues(); I != E; ++I)
        Results.push_back(Res.getValue(I));
      break;
    }
    // Convert add (x, imm) into sub (x, -imm).
    if (const ConstantSDNode *C = dyn_cast<ConstantSDNode>(N->getOperand(1))) {
      SDValue Sub = DAG.getNode(
          ISD::SUB, DL, N->getValueType(0), N->getOperand(0),
          DAG.getConstant(-C->getAPIntValue(), DL, C->getValueType(0)));
      Results.push_back(Sub);
    }
    break;
  }
  case ISD::SUB: {
    // For i64, emit a libcall to __subdi3.
    if (N->getValueType(0) == MVT::i64) {
      SDValue Res = LowerOperation(SDValue(N, 0), DAG);
      for (unsigned I = 0, E = Res->getNumValues(); I != E; ++I)
        Results.push_back(Res.getValue(I));
      break;
    }
    break;
  }
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR: {
    // For i64, emit a libcall to __anddi3/__ordi3/__xordi3.
    if (N->getValueType(0) == MVT::i64) {
      SDValue Res = LowerOperation(SDValue(N, 0), DAG);
      for (unsigned I = 0, E = Res->getNumValues(); I != E; ++I)
        Results.push_back(Res.getValue(I));
      break;
    }
    break;
  }
  default: {
    SDValue Res = LowerOperation(SDValue(N, 0), DAG);

    for (unsigned I = 0, E = Res->getNumValues(); I != E; ++I)
      Results.push_back(Res.getValue(I));

    break;
  }
  }
}

SDValue I8085TargetLowering::PerformDAGCombine(SDNode *N,
                                             DAGCombinerInfo &DCI) const {
  switch (N->getOpcode()) {
  case ISD::ADD:
    return performAddSubCombine(N, DCI);
  case ISD::SUB: {
    if (SDValue V = performAddSubCombine(N, DCI))
      return V;
    return performSubCombine(N, DCI);
  }
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR:
    return performLogicCombine(N, DCI);
  case ISD::SHL:
  case ISD::SRL:
  case ISD::SRA:
    return performShiftCombine(N, DCI);
  case ISD::MUL:
    return performMulCombine(N, DCI);
  case ISD::TRUNCATE:
    return performTruncMulCombine(N, DCI);
  case ISD::UDIV:
    return performUDivCombine(N, DCI);
  case ISD::UREM:
    return performURemCombine(N, DCI);
  default:
    break;
  }
  return SDValue();
}

SDValue I8085TargetLowering::performSubCombine(SDNode *N,
                                             DAGCombinerInfo &DCI) const {
  EVT VT = N->getValueType(0);
  if (VT != MVT::i8 && VT != MVT::i16)
    return SDValue();

  auto *CLHS = dyn_cast<ConstantSDNode>(N->getOperand(0));
  if (!CLHS)
    return SDValue();
  if (CLHS->isZero())
    return SDValue();

  if (isa<ConstantSDNode>(N->getOperand(1)))
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);
  SDValue RHS = N->getOperand(1);
  SDValue AllOnes = DAG.getAllOnesConstant(DL, VT);
  SDValue NotRHS = DAG.getNode(ISD::XOR, DL, VT, RHS, AllOnes);
  APInt C = CLHS->getAPIntValue();
  SDValue CPlusOne = DAG.getConstant(C + 1, DL, VT);
  return DAG.getNode(ISD::ADD, DL, VT, NotRHS, CPlusOne);
}

SDValue I8085TargetLowering::performMulCombine(SDNode *N,
                                             DAGCombinerInfo &DCI) const {
  EVT VT = N->getValueType(0);
  if (!isIntegerVT(VT))
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  // Pattern 1: mul i32 (sext i16, sext i16) -> __mulsi16 returning i32
  // This is handled via Custom lowering of MUL i32 in LowerOperation,
  // not here, to avoid issues with LowerCallTo inside DAG combine.
  // The truncate-specialized patterns (shr8, hi16, lo16) are handled
  // in performTruncMulCombine, which fires on TRUNCATE nodes.

  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  const ConstantSDNode *C = dyn_cast<ConstantSDNode>(LHS);
  SDValue Other = RHS;
  if (!C) {
    C = dyn_cast<ConstantSDNode>(RHS);
    Other = LHS;
  }

  unsigned ShiftAmt = 0;
  if (!getPowerOf2ShiftAmount(C, ShiftAmt))
    ShiftAmt = 0;

  if (!C)
    return SDValue();
  if (C->isZero())
    return DAG.getConstant(0, DL, VT);
  if (C->isOne())
    return Other;
  if (!ShiftAmt)
    return SDValue();

  EVT ShiftVT = getShiftAmountTy(VT, DAG.getDataLayout());
  SDValue Amt = DAG.getConstant(ShiftAmt, DL, ShiftVT);
  return DAG.getNode(ISD::SHL, DL, VT, Other, Amt);
}

SDValue I8085TargetLowering::performTruncMulCombine(SDNode *N,
                                                   DAGCombinerInfo &DCI) const {
  EVT VT = N->getValueType(0);
  SDValue Src = N->getOperand(0);
  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);
  SDValue A, B;

  // --- Truncate i32 to i16 patterns involving mul(sext i16, sext i16) ---
  if (VT == MVT::i16 && Src.getValueType() == MVT::i32) {
    // Pattern 2: trunc(lshr(mul(sext, sext), 8)) -> __mulsi16_shr8
    // Pattern 3: trunc(lshr(mul(sext, sext), 16)) -> __mulsi16_hi16
    // Only match when the shift and mul have single uses to avoid
    // double-lowering when the full i32 result is also needed.
    if (Src.getOpcode() == ISD::SRL && Src.hasOneUse()) {
      if (auto *ShAmt = dyn_cast<ConstantSDNode>(Src.getOperand(1))) {
        SDValue MulVal = Src.getOperand(0);
        if (ShAmt->getZExtValue() == 8 && isMulSExtI16(MulVal, A, B) &&
            MulVal.hasOneUse()) {
          return emitMul16LibCall(DAG, DL, "__mulsi16_shr8", MVT::i16, A, B,
                                  *this);
        }
        if (ShAmt->getZExtValue() == 16 && isMulSExtI16(MulVal, A, B) &&
            MulVal.hasOneUse()) {
          return emitMul16LibCall(DAG, DL, "__mulsi16_hi16", MVT::i16, A, B,
                                  *this);
        }
      }
    }

    // Also match SRA (arithmetic shift right) which produces the same
    // result for these bit positions.
    if (Src.getOpcode() == ISD::SRA && Src.hasOneUse()) {
      if (auto *ShAmt = dyn_cast<ConstantSDNode>(Src.getOperand(1))) {
        SDValue MulVal = Src.getOperand(0);
        if (ShAmt->getZExtValue() == 8 && isMulSExtI16(MulVal, A, B) &&
            MulVal.hasOneUse()) {
          return emitMul16LibCall(DAG, DL, "__mulsi16_shr8", MVT::i16, A, B,
                                  *this);
        }
        if (ShAmt->getZExtValue() == 16 && isMulSExtI16(MulVal, A, B) &&
            MulVal.hasOneUse()) {
          return emitMul16LibCall(DAG, DL, "__mulsi16_hi16", MVT::i16, A, B,
                                  *this);
        }
      }
    }

    // Pattern 4: trunc(mul(sext, sext)) -> __mulsi16_lo16
    // Only match if the MUL is used ONLY by this truncate (i.e., nobody else
    // needs the full i32 result). Otherwise, let custom lowering handle the
    // MUL as __mulsi16 and the truncate will be a cheap register extraction.
    if (isMulSExtI16(Src, A, B) && Src.hasOneUse()) {
      return emitMul16LibCall(DAG, DL, "__mulsi16_lo16", MVT::i16, A, B, *this);
    }
  }

  // --- Truncate i64 to i32 patterns involving mul(sext i32, sext i32) ---
  if (VT == MVT::i32 && Src.getValueType() == MVT::i64) {
    // Pattern: trunc i32 (srl/sra (mul(sext i32, sext i32), 16)) -> __mulsi32_shr16
    // Pattern: trunc i32 (srl/sra (mul(sext i32, sext i32), 32)) -> __mulsi32_hi32
    //
    // The SRL/SRA may have multiple TRUNCATE users (e.g., trunc to i16 for
    // register-pair splitting). We replace the SRL itself via CombineTo so
    // all users see the library-call result.
    if (Src.getOpcode() == ISD::SRL || Src.getOpcode() == ISD::SRA) {
      if (auto *ShAmt = dyn_cast<ConstantSDNode>(Src.getOperand(1))) {
        SDValue MulVal = Src.getOperand(0);
        if ((ShAmt->getZExtValue() == 16 || ShAmt->getZExtValue() == 32) &&
            isMulSExtI32(MulVal, A, B) && MulVal.hasOneUse()) {
          const char *Name = (ShAmt->getZExtValue() == 16)
                                 ? "__mulsi32_shr16"
                                 : "__mulsi32_hi32";
          SDValue CallResult =
              emitMul32LibCall(DAG, DL, Name, MVT::i32, A, B, *this);
          // Replace the entire SRL i64 node with build_pair(result, undef).
          // The low i32 is the library call result; the upper i32 is unused
          // (any remaining truncates only look at the low bits).
          SDValue Hi = DAG.getUNDEF(MVT::i32);
          SDValue Pair = DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i64,
                                     CallResult, Hi);
          DCI.CombineTo(Src.getNode(), Pair);
          // Return the i32 result directly for this truncate node.
          return CallResult;
        }
      }
    }

    // Pattern: trunc i32 (mul(sext i32, sext i32)) -> __mulsi32_lo32
    // The low 32 bits of sext multiply == low 32 bits of unsigned multiply,
    // which is just __mul32.  No special routine needed -- let standard
    // lowering handle it.
  }

  // --- Truncate i16 to i8 patterns involving mul(sext i8, sext i8) ---
  if (VT == MVT::i8 && Src.getValueType() == MVT::i16) {
    // Pattern: trunc i8 (srl/sra (mul i16 (sext i8, sext i8)), 8) -> __mulsi8_hi8
    if ((Src.getOpcode() == ISD::SRL || Src.getOpcode() == ISD::SRA) &&
        Src.hasOneUse()) {
      if (auto *ShAmt = dyn_cast<ConstantSDNode>(Src.getOperand(1))) {
        SDValue MulVal = Src.getOperand(0);
        if (ShAmt->getZExtValue() == 8 && isMulSExtI8(MulVal, A, B) &&
            MulVal.hasOneUse()) {
          return emitMul8LibCall(DAG, DL, "__mulsi8_hi8", MVT::i8, A, B,
                                 *this);
        }
      }
    }

    // Pattern: trunc i8 (mul i16 (sext i8, sext i8)) -> __mulsi8_lo8
    if (isMulSExtI8(Src, A, B) && Src.hasOneUse()) {
      return emitMul8LibCall(DAG, DL, "__mulsi8_lo8", MVT::i8, A, B, *this);
    }
  }

  return SDValue();
}

SDValue I8085TargetLowering::performUDivCombine(SDNode *N,
                                              DAGCombinerInfo &DCI) const {
  EVT VT = N->getValueType(0);
  if (!isIntegerVT(VT))
    return SDValue();

  const ConstantSDNode *C = dyn_cast<ConstantSDNode>(N->getOperand(1));
  unsigned ShiftAmt = 0;
  if (!C)
    return SDValue();
  if (C->isOne())
    return N->getOperand(0);
  if (!getPowerOf2ShiftAmount(C, ShiftAmt))
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);
  EVT ShiftVT = getShiftAmountTy(VT, DAG.getDataLayout());
  SDValue Amt = DAG.getConstant(ShiftAmt, DL, ShiftVT);
  return DAG.getNode(ISD::SRL, DL, VT, N->getOperand(0), Amt);
}

SDValue I8085TargetLowering::performURemCombine(SDNode *N,
                                              DAGCombinerInfo &DCI) const {
  EVT VT = N->getValueType(0);
  if (!isIntegerVT(VT))
    return SDValue();

  const ConstantSDNode *C = dyn_cast<ConstantSDNode>(N->getOperand(1));
  unsigned ShiftAmt = 0;
  if (!C)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);
  if (C->isOne())
    return DAG.getConstant(0, DL, VT);
  if (!getPowerOf2ShiftAmount(C, ShiftAmt))
    return SDValue();

  APInt Mask = C->getAPIntValue() - 1;
  SDValue MaskVal = DAG.getConstant(Mask, DL, VT);
  return DAG.getNode(ISD::AND, DL, VT, N->getOperand(0), MaskVal);
}

SDValue I8085TargetLowering::performAddSubCombine(SDNode *N,
                                                DAGCombinerInfo &DCI) const {
  EVT VT = N->getValueType(0);
  if (!isIntegerVT(VT))
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  // Fold ADD(X, SELECT(cond, 0, Y)) -> SELECT(cond, X, ADD(X, Y))
  // and  ADD(SELECT(cond, 0, Y), X) -> SELECT(cond, X, ADD(X, Y))
  // On the 8085, branch-based SELECT is much cheaper than always executing
  // the ADD. This undoes an InstCombine transform that pulled the ADD out
  // of the select.
  if (N->getOpcode() == ISD::ADD && VT.getSizeInBits() > 16) {
    for (int i = 0; i < 2; ++i) {
      SDValue Sel = N->getOperand(i);
      SDValue Other = N->getOperand(1 - i);
      if (Sel.getOpcode() == ISD::SELECT && Sel.hasOneUse()) {
        SDValue Cond = Sel.getOperand(0);
        SDValue TVal = Sel.getOperand(1);
        SDValue FVal = Sel.getOperand(2);
        // Match SELECT(cond, 0, Y) -> fold to SELECT(cond, Other, ADD(Other, Y))
        if (auto *TC = dyn_cast<ConstantSDNode>(TVal)) {
          if (TC->isZero()) {
            SDValue NewAdd = DAG.getNode(ISD::ADD, DL, VT, Other, FVal,
                                         N->getFlags());
            return DAG.getSelect(DL, VT, Cond, Other, NewAdd);
          }
        }
        // Match SELECT(cond, Y, 0) -> fold to SELECT(cond, ADD(Other, Y), Other)
        if (auto *FC = dyn_cast<ConstantSDNode>(FVal)) {
          if (FC->isZero()) {
            SDValue NewAdd = DAG.getNode(ISD::ADD, DL, VT, Other, TVal,
                                         N->getFlags());
            return DAG.getSelect(DL, VT, Cond, NewAdd, Other);
          }
        }
      }
    }
  }

  const ConstantSDNode *C = dyn_cast<ConstantSDNode>(RHS);
  if (!C)
    return SDValue();
  if (!C->isZero())
    return SDValue();

  return LHS;
}



SDValue I8085TargetLowering::performLogicCombine(SDNode *N,
                                               DAGCombinerInfo &DCI) const {
  EVT VT = N->getValueType(0);
  if (!isIntegerVT(VT))
    return SDValue();

  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  // Recognize byte packing pattern for i16:
  // (or (shl (zext i8 to i16), 8), (zext i8 to i16)) -> pack bytes
  // This pattern is common when building 16-bit values from two bytes.
  if (N->getOpcode() == ISD::OR && VT == MVT::i16) {
    SDValue Hi, Lo;
    SDValue HiSrc, LoSrc;

    // Try both orderings: (or (shl hi, 8), lo) and (or lo, (shl hi, 8))
    auto tryMatch = [&](SDValue Shifted, SDValue Other) -> bool {
      if (Shifted.getOpcode() == ISD::SHL) {
        if (auto *ShAmt = dyn_cast<ConstantSDNode>(Shifted.getOperand(1))) {
          if (ShAmt->getZExtValue() == 8) {
            SDValue ShiftedVal = Shifted.getOperand(0);
            if (isZExtFromI8(ShiftedVal, HiSrc) && isZExtFromI8(Other, LoSrc)) {
              Hi = ShiftedVal;
              Lo = Other;
              return true;
            }
          }
        }
      }
      return false;
    };

    if (tryMatch(LHS, RHS) || tryMatch(RHS, LHS)) {
      // We've matched the pack pattern.
      // Generate: BUILD_PAIR(lo_byte, hi_byte) which is more efficient
      // on i8085 as it avoids the shift entirely.
      // For now, we just recognize the pattern - the existing code will
      // handle it via the shift combining. The real optimization is that
      // this pattern informs the instruction selector.
      // We can return the original if no better pattern is available,
      // but marking it allows future optimizations.
      // For i8085, we leave it as-is since our shift by 8 is already
      // optimized to byte moves.
    }
  }

  const ConstantSDNode *C = dyn_cast<ConstantSDNode>(RHS);
  if (!C)
    return SDValue();

  APInt Val = C->getAPIntValue();
  switch (N->getOpcode()) {
  case ISD::AND:
    if (Val.isAllOnes())
      return LHS;
    if (Val.isZero())
      return DAG.getConstant(0, DL, VT);
    break;
  case ISD::OR:
    if (Val.isZero())
      return LHS;
    if (Val.isAllOnes())
      return DAG.getConstant(Val, DL, VT);
    break;
  case ISD::XOR:
    if (Val.isZero())
      return LHS;
    break;
  default:
    break;
  }
  return SDValue();
}

SDValue I8085TargetLowering::performShiftCombine(SDNode *N,
                                               DAGCombinerInfo &DCI) const {
  EVT VT = N->getValueType(0);
  if (!isIntegerVT(VT))
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  const ConstantSDNode *C = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!C)
    return SDValue();

  // Remove shifts by zero.
  if (C->isZero())
    return N->getOperand(0);

  // Recognize sign-extension pattern: (sra (shl x, N), N) -> sext
  // This pattern appears when sext i16 to i32 is legalized to shifts.
  // Converting it back allows matching the efficient SEXT16TO32 instruction.
  if (N->getOpcode() == ISD::SRA && VT == MVT::i32) {
    unsigned ShiftAmt = C->getZExtValue();
    SDValue ShiftIn = N->getOperand(0);

    // Check if the input is (shl x, ShiftAmt)
    if (ShiftIn.getOpcode() == ISD::SHL) {
      const ConstantSDNode *ShlAmt =
          dyn_cast<ConstantSDNode>(ShiftIn.getOperand(1));
      if (ShlAmt && ShlAmt->getZExtValue() == ShiftAmt) {
        SDValue OrigVal = ShiftIn.getOperand(0);

        // (sra (shl x, 16), 16) is sign-extend from i16 to i32
        if (ShiftAmt == 16) {
          // Check if the original value comes from a zero/any extend of i16
          if (OrigVal.getOpcode() == ISD::ZERO_EXTEND ||
              OrigVal.getOpcode() == ISD::ANY_EXTEND) {
            SDValue ExtendIn = OrigVal.getOperand(0);
            if (ExtendIn.getValueType() == MVT::i16) {
              // Replace with sign_extend i16 -> i32
              return DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i32, ExtendIn);
            }
          }
          // Also handle the case where OrigVal is already i32 but was extended
          // Check if the upper 16 bits are known to be zero (from earlier zext)
          // In that case, we can still use sign_extend by truncating first
          if (OrigVal.getValueType() == MVT::i32) {
            // Truncate to i16 then sign extend back to i32
            SDValue Trunc = DAG.getNode(ISD::TRUNCATE, DL, MVT::i16, OrigVal);
            return DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i32, Trunc);
          }
        }

        // (sra (shl x, 24), 24) is sign-extend from i8 to i32
        if (ShiftAmt == 24) {
          if (OrigVal.getOpcode() == ISD::ZERO_EXTEND ||
              OrigVal.getOpcode() == ISD::ANY_EXTEND) {
            SDValue ExtendIn = OrigVal.getOperand(0);
            if (ExtendIn.getValueType() == MVT::i8) {
              return DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i32, ExtendIn);
            }
          }
          if (OrigVal.getValueType() == MVT::i32) {
            SDValue Trunc = DAG.getNode(ISD::TRUNCATE, DL, MVT::i8, OrigVal);
            return DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i32, Trunc);
          }
        }

        // (sra (shl x, 31), 31) is sign-extend from i1 to i32.
        // This pattern appears when select is converted to arithmetic form.
        // On i8085, 31-bit shifts are extremely expensive. Replace with:
        //   bit0 = trunc(x) & 1; result = 0 - zext(bit0)
        // This avoids the 31-bit shift entirely.
        if (ShiftAmt == 31) {
          SDValue Trunc = DAG.getNode(ISD::TRUNCATE, DL, MVT::i8, OrigVal);
          SDValue One8 = DAG.getConstant(1, DL, MVT::i8);
          SDValue Bit0 = DAG.getNode(ISD::AND, DL, MVT::i8, Trunc, One8);
          SDValue ZExt = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i32, Bit0);
          SDValue Zero32 = DAG.getConstant(0, DL, MVT::i32);
          return DAG.getNode(ISD::SUB, DL, MVT::i32, Zero32, ZExt);
        }
      }
    }
  }

  // Same pattern for i16: (sra (shl x, 8), 8) is sign-extend from i8 to i16
  if (N->getOpcode() == ISD::SRA && VT == MVT::i16) {
    unsigned ShiftAmt = C->getZExtValue();
    SDValue ShiftIn = N->getOperand(0);

    if (ShiftAmt == 8 && ShiftIn.getOpcode() == ISD::SHL) {
      const ConstantSDNode *ShlAmt =
          dyn_cast<ConstantSDNode>(ShiftIn.getOperand(1));
      if (ShlAmt && ShlAmt->getZExtValue() == 8) {
        SDValue OrigVal = ShiftIn.getOperand(0);
        if (OrigVal.getOpcode() == ISD::ZERO_EXTEND ||
            OrigVal.getOpcode() == ISD::ANY_EXTEND) {
          SDValue ExtendIn = OrigVal.getOperand(0);
          if (ExtendIn.getValueType() == MVT::i8) {
            return DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i16, ExtendIn);
          }
        }
        if (OrigVal.getValueType() == MVT::i16) {
          SDValue Trunc = DAG.getNode(ISD::TRUNCATE, DL, MVT::i8, OrigVal);
          return DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i16, Trunc);
        }
      }
    }
  }

  return SDValue();
}

/// Return true if the addressing mode represented
/// by AM is legal for this target, for a load/store of the specified type.
bool I8085TargetLowering::isLegalAddressingMode(const DataLayout &DL,
                                              const AddrMode &AM, Type *Ty,
                                              unsigned AS,
                                              Instruction *I) const {
  int64_t Offs = AM.BaseOffs;

  // Allow absolute addresses.
  if (AM.BaseGV && !AM.HasBaseReg && AM.Scale == 0 && Offs == 0) {
    return true;
  }

  // Flash memory instructions only allow zero offsets.
  if (isa<PointerType>(Ty) && AS == I8085::ProgramMemory) {
    return false;
  }

  // Allow reg+<6bit> offset.
  if (Offs < 0)
    Offs = -Offs;
  if (AM.BaseGV == nullptr && AM.HasBaseReg && AM.Scale == 0 &&
      isUInt<6>(Offs)) {
    return true;
  }

  return false;
}

/// Returns true by value, base pointer and
/// offset pointer and addressing mode by reference if the node's address
/// can be legally represented as pre-indexed load / store address.
bool I8085TargetLowering::getPreIndexedAddressParts(SDNode *N, SDValue &Base,
                                                  SDValue &Offset,
                                                  ISD::MemIndexedMode &AM,
                                                  SelectionDAG &DAG) const {
  EVT VT;
  const SDNode *Op;
  SDLoc DL(N);

  if (const LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    VT = LD->getMemoryVT();
    Op = LD->getBasePtr().getNode();
    if (LD->getExtensionType() != ISD::NON_EXTLOAD)
      return false;
    if (I8085::isProgramMemoryAccess(LD)) {
      return false;
    }
  } else if (const StoreSDNode *ST = dyn_cast<StoreSDNode>(N)) {
    VT = ST->getMemoryVT();
    Op = ST->getBasePtr().getNode();
    if (I8085::isProgramMemoryAccess(ST)) {
      return false;
    }
  } else {
    return false;
  }

  if (VT != MVT::i8 && VT != MVT::i16) {
    return false;
  }

  if (Op->getOpcode() != ISD::ADD && Op->getOpcode() != ISD::SUB) {
    return false;
  }

  if (const ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(Op->getOperand(1))) {
    int RHSC = RHS->getSExtValue();
    if (Op->getOpcode() == ISD::SUB)
      RHSC = -RHSC;

    if ((VT == MVT::i16 && RHSC != -2) || (VT == MVT::i8 && RHSC != -1)) {
      return false;
    }

    Base = Op->getOperand(0);
    Offset = DAG.getConstant(RHSC, DL, MVT::i8);
    AM = ISD::PRE_DEC;

    return true;
  }

  return false;
}

/// Returns true by value, base pointer and
/// offset pointer and addressing mode by reference if this node can be
/// combined with a load / store to form a post-indexed load / store.
bool I8085TargetLowering::getPostIndexedAddressParts(SDNode *N, SDNode *Op,
                                                   SDValue &Base,
                                                   SDValue &Offset,
                                                   ISD::MemIndexedMode &AM,
                                                   SelectionDAG &DAG) const {
  EVT VT;
  SDLoc DL(N);

  if (const LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    VT = LD->getMemoryVT();
    if (LD->getExtensionType() != ISD::NON_EXTLOAD)
      return false;
  } else if (const StoreSDNode *ST = dyn_cast<StoreSDNode>(N)) {
    VT = ST->getMemoryVT();
    if (I8085::isProgramMemoryAccess(ST)) {
      return false;
    }
  } else {
    return false;
  }

  if (VT != MVT::i8 && VT != MVT::i16) {
    return false;
  }

  if (Op->getOpcode() != ISD::ADD && Op->getOpcode() != ISD::SUB) {
    return false;
  }

  if (const ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(Op->getOperand(1))) {
    int RHSC = RHS->getSExtValue();
    if (Op->getOpcode() == ISD::SUB)
      RHSC = -RHSC;
    if ((VT == MVT::i16 && RHSC != 2) || (VT == MVT::i8 && RHSC != 1)) {
      return false;
    }

    Base = Op->getOperand(0);
    Offset = DAG.getConstant(RHSC, DL, MVT::i8);
    AM = ISD::POST_INC;

    return true;
  }

  return false;
}

bool I8085TargetLowering::isOffsetFoldingLegal(
    const GlobalAddressSDNode *GA) const {
  return true;
}

//===----------------------------------------------------------------------===//
//             Formal Arguments Calling Convention Implementation
//===----------------------------------------------------------------------===//

#include "I8085GenCallingConv.inc"

/// Registers for calling conventions, ordered in reverse as required by ABI.
/// Both arrays must be of the same length.
static const MCPhysReg LLVM_ATTRIBUTE_UNUSED RegList8I8085[] = {
    I8085::B, I8085::C, I8085::D, I8085::E};

static const MCPhysReg LLVM_ATTRIBUTE_UNUSED RegList16I8085[] = {
    I8085::BC, I8085::DE};





/// Analyze incoming and outgoing function arguments. We need custom C++ code
/// to handle special constraints in the ABI.
/// In addition, all pieces of a certain argument have to be passed either
/// using registers or the stack but never mixing both.
template <typename ArgT>
static void analyzeArguments(TargetLowering::CallLoweringInfo *CLI,
                             const Function *F, const DataLayout *TD,
                             const SmallVectorImpl<ArgT> &Args,
                             SmallVectorImpl<CCValAssign> &ArgLocs,
                             CCState &CCInfo, bool Tiny) {
  // Choose the proper register list for argument passing according to the ABI.
  ArrayRef<MCPhysReg> RegList8;
  ArrayRef<MCPhysReg> RegList16;

    RegList8 = ArrayRef(RegList8I8085, std::size(RegList8I8085));
    RegList16 = ArrayRef(RegList16I8085, std::size(RegList16I8085));


  unsigned NumArgs = Args.size();
  // This is the index of the last used register, in RegList*.
  // -1 means R26 (R26 is never actually used in CC).
  int RegLastIdx = -1;
  // Once a value is passed to the stack it will always be used
  bool UseStack = false;
  for (unsigned i = 0; i != NumArgs;) {
    MVT VT = Args[i].VT;
    // We have to count the number of bytes for each function argument, that is
    // those Args with the same OrigArgIndex. This is important in case the
    // function takes an aggregate type.
    // Current argument will be between [i..j).
    unsigned ArgIndex = Args[i].OrigArgIndex;
    unsigned TotalBytes = VT.getStoreSize();
    unsigned j = i + 1;
    for (; j != NumArgs; ++j) {
      if (Args[j].OrigArgIndex != ArgIndex)
        break;
      TotalBytes += Args[j].VT.getStoreSize();
    }
    // Round up to even number of bytes.
    TotalBytes = alignTo(TotalBytes, 2);
    // Skip zero sized arguments
    if (TotalBytes == 0)
      continue;
    // The index of the first register to be used
    unsigned RegIdx = RegLastIdx + TotalBytes;
    RegLastIdx = RegIdx;
    // If there are not enough registers, use the stack
    if (RegIdx >= RegList8.size()) {
      UseStack = true;
    }
    for (; i != j; ++i) {
      MVT VT = Args[i].VT;

      if (UseStack) {
        auto evt = EVT(VT).getTypeForEVT(CCInfo.getContext());
        unsigned Offset = CCInfo.AllocateStack(TD->getTypeAllocSize(evt),
                                               TD->getABITypeAlign(evt));
        CCInfo.addLoc(
            CCValAssign::getMem(i, VT, Offset, VT, CCValAssign::Full));
      } else {
        unsigned Reg;
        if (VT == MVT::i8) {
          Reg = CCInfo.AllocateReg(RegList8[RegIdx]);
        } else if (VT == MVT::i16) {
          Reg = CCInfo.AllocateReg(RegList16[RegIdx]);
        } else {
          llvm_unreachable(
              "calling convention can only manage i8 and i16 types");
        }
        assert(Reg && "register not available in calling convention");
        CCInfo.addLoc(CCValAssign::getReg(i, VT, Reg, VT, CCValAssign::Full));
        // Registers inside a particular argument are sorted in increasing order
        // (remember the array is reversed).
        RegIdx -= VT.getStoreSize();
      }
    }
  }
}

/// Count the total number of bytes needed to pass or return these arguments.
template <typename ArgT>
static unsigned
getTotalArgumentsSizeInBytes(const SmallVectorImpl<ArgT> &Args) {
  unsigned TotalBytes = 0;

  for (const ArgT &Arg : Args) {
    TotalBytes += Arg.VT.getStoreSize();
  }
  return TotalBytes;
}

/// Analyze incoming and outgoing value of returning from a function.
/// The algorithm is similar to analyzeArguments, but there can only be
/// one value, possibly an aggregate, and it is limited to 8 bytes.
template <typename ArgT>
static void analyzeReturnValues(const SmallVectorImpl<ArgT> &Args,
                                CCState &CCInfo, bool Tiny) {
  unsigned NumArgs = Args.size();
  unsigned TotalBytes = getTotalArgumentsSizeInBytes(Args);
  // CanLowerReturn() guarantees this assertion.
  assert(TotalBytes <= 8 &&
         "return values greater than 8 bytes cannot be lowered");

  // Choose the proper register list for argument passing according to the ABI.
  ArrayRef<MCPhysReg> RegList8;
  ArrayRef<MCPhysReg> RegList16;

    RegList8 = ArrayRef(RegList8I8085, std::size(RegList8I8085));
    RegList16 = ArrayRef(RegList16I8085, std::size(RegList16I8085));


  // GCC-ABI says that the size is rounded up to the next even number,
  // but actually once it is more than 4 it will always round up to 8.
  if (TotalBytes > 4) {
    TotalBytes = 8;
  } else {
    TotalBytes = alignTo(TotalBytes, 2);
  }

  // The index of the first register to use.
  int RegIdx = TotalBytes - 1;
  for (unsigned i = 0; i != NumArgs; ++i) {
    MVT VT = Args[i].VT;
    unsigned Reg;
    if (VT == MVT::i8) {
      Reg = CCInfo.AllocateReg(RegList8[RegIdx]);
    } else if (VT == MVT::i16) {
      Reg = CCInfo.AllocateReg(RegList16[RegIdx]);
    } else {
      llvm_unreachable("calling convention can only manage i8 and i16 types");
    }
    assert(Reg && "register not available in calling convention");
    CCInfo.addLoc(CCValAssign::getReg(i, VT, Reg, VT, CCValAssign::Full));
    // Registers sort in increasing order
    RegIdx -= VT.getStoreSize();
  }
}

SDValue I8085TargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto DL = DAG.getDataLayout();

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());

  // Use fast CC for internal functions promoted by GlobalOpt.
  if (CallConv == CallingConv::Fast && !isVarArg)
    CCInfo.AnalyzeFormalArguments(Ins, ArgCC_I8085_Fast);
  else
    CCInfo.AnalyzeFormalArguments(Ins, ArgCC_I8085_Vararg);


  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    const ISD::InputArg &Arg = Ins[i];

    // Arguments stored on registers.
    if (VA.isRegLoc()) {
      const TargetRegisterClass *RC;
      if (VA.getLocVT() == MVT::i8)
        RC = &I8085::GR8RegClass;
      else
        RC = &I8085::GR16RegClass;

      unsigned Reg = MF.addLiveIn(VA.getLocReg(), RC);
      SDValue ArgValue = DAG.getCopyFromReg(Chain, dl, Reg, VA.getLocVT());
      InVals.push_back(ArgValue);
    } else {
      // Only arguments passed on the stack should make it here.
      assert(VA.isMemLoc());

      EVT LocVT = VA.getLocVT();

      int32_t StackOffset = VA.getLocMemOffset();

      if (Arg.Flags.isByVal() && Arg.Flags.getByValSize()) {
        unsigned Size = Arg.Flags.getByValSize();
        Align Alignment =
            std::max(Align(1), Arg.Flags.getNonZeroByValAlign());
        int FI = MFI.CreateFixedObject(Size, StackOffset, true);
        MFI.setObjectAlignment(FI, Alignment);
        SDValue FIN = DAG.getFrameIndex(FI, getPointerTy(DL));
        InVals.push_back(FIN);
        continue;
      }

      // Create the frame index object for this incoming parameter.
      int FI = MFI.CreateFixedObject(LocVT.getSizeInBits() / 8,
                                     StackOffset, true);

      // Create the SelectionDAG nodes corresponding to a load
      // from this parameter.
      SDValue FIN = DAG.getFrameIndex(FI, getPointerTy(DL));

      SDValue load = DAG.getLoad(LocVT, dl, Chain, FIN,
                                 MachinePointerInfo::getFixedStack(MF, FI));

      InVals.push_back(load);
    }
  }

  // If the function takes variable number of arguments, make a frame index for
  // the start of the first vararg value... for expansion of llvm.va_start.
  if (isVarArg) {
    unsigned StackSize = CCInfo.getStackSize();
    I8085MachineFunctionInfo *AFI = MF.getInfo<I8085MachineFunctionInfo>();

    AFI->setVarArgsFrameIndex(MFI.CreateFixedObject(2, StackSize, true));
  }

  return Chain;
}

uint8_t twos_complement(uint8_t val) { return -(unsigned int)val;}

//===----------------------------------------------------------------------===//
//                  Call Calling Convention Implementation
//===----------------------------------------------------------------------===//

static bool isEligibleForTailCallOptimization(
    TargetLowering::CallLoweringInfo &CLI, CCState &CCInfo,
    MachineFunction &MF) {
  if (!CLI.IsTailCall)
    return false;

  const Function &Caller = MF.getFunction();
  if (Caller.getFnAttribute("disable-tail-calls").getValueAsString() == "true")
    return false;

  if (CLI.IsVarArg)
    return false;

  if (CCInfo.getStackSize() != 0)
    return false;

  if (MF.getFrameInfo().hasVarSizedObjects())
    return false;

  for (const ISD::OutputArg &Out : CLI.Outs) {
    if (Out.Flags.isByVal() || Out.Flags.isSRet() || Out.Flags.isInReg() ||
        Out.Flags.isNest())
      return false;
  }

  return true;
}


SDValue I8085TargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                     SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &DL = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  bool &isTailCall = CLI.IsTailCall;
  CallingConv::ID CallConv = CLI.CallConv;
  bool isVarArg = CLI.IsVarArg;

  MachineFunction &MF = DAG.getMachineFunction();

  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());


  // Use fast CC for internal functions promoted by GlobalOpt.
  if (CallConv == CallingConv::Fast && !isVarArg)
    CCInfo.AnalyzeCallOperands(Outs, ArgCC_I8085_Fast);
  else
    CCInfo.AnalyzeCallOperands(Outs, ArgCC_I8085_Vararg);

  isTailCall = isEligibleForTailCallOptimization(CLI, CCInfo, MF);

  // If the callee is a GlobalAddress/ExternalSymbol node (quite common, every
  // direct call is) turn it into a TargetGlobalAddress/TargetExternalSymbol
  // node so that legalize doesn't hack it.
  if (const GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    const GlobalValue *GV = G->getGlobal();
    Callee =
        DAG.getTargetGlobalAddress(GV, DL, getPointerTy(DAG.getDataLayout()));
  } else if (const ExternalSymbolSDNode *ES =
                 dyn_cast<ExternalSymbolSDNode>(Callee)) {
    Callee = DAG.getTargetExternalSymbol(ES->getSymbol(),
                                         getPointerTy(DAG.getDataLayout()));
  }

  // Get a count of how many bytes are to be pushed on the stack.
  unsigned NumBytes = CCInfo.getStackSize();


  SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPass;

  // Walk all arg locations and separate register vs stack args.
  // With fastcc, register and stack args may be interleaved (e.g., i32 on
  // stack, then ptr in register), so we cannot assume register args come first.
  bool HasStackArgs = false;
  for (unsigned AI = 0, AE = ArgLocs.size(); AI != AE; ++AI) {
    CCValAssign &VA = ArgLocs[AI];
    SDValue Arg = OutVals[AI];

    if (VA.isRegLoc()) {
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
    } else {
      HasStackArgs = true;
    }
  }

  if (!isTailCall)
    Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, DL);

  // Walk stack arguments and emit stores.
  if (HasStackArgs) {
    SmallVector<SDValue, 8> MemOpChains;
    for (unsigned AI = 0, AE = ArgLocs.size(); AI != AE; ++AI) {
      CCValAssign &VA = ArgLocs[AI];
      if (!VA.isMemLoc())
        continue;

      SDValue Arg = OutVals[AI];
      ISD::ArgFlagsTy Flags = Outs[AI].Flags;

      SDValue PtrOff = DAG.getNode(
          ISD::ADD, DL, getPointerTy(DAG.getDataLayout()),
          DAG.getRegister(I8085::SP, getPointerTy(DAG.getDataLayout())),
          DAG.getIntPtrConstant(VA.getLocMemOffset(), DL));

      if (Flags.isByVal() && Flags.getByValSize()) {
        unsigned Size = Flags.getByValSize();
        Align Alignment =
            std::max(Align(1), Flags.getNonZeroByValAlign());
        SDValue SizeVal = DAG.getConstant(Size, DL, getPointerTy(DAG.getDataLayout()));
        MemOpChains.push_back(DAG.getMemcpy(
            Chain, DL, PtrOff, Arg, SizeVal, Alignment,
            /*isVolatile=*/false, /*AlwaysInline=*/false, /*CI=*/nullptr,
            std::nullopt, MachinePointerInfo(), MachinePointerInfo()));
      } else {
        MemOpChains.push_back(DAG.getStore(
            Chain, DL, Arg, PtrOff,
            MachinePointerInfo::getStack(MF, VA.getLocMemOffset())));
      }
    }

    if (!MemOpChains.empty())
      Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOpChains);
  }

  // Build a sequence of copy-to-reg nodes chained together with token chain and
  // flag operands which copy the outgoing args into registers.  The InFlag in
  // necessary since all emited instructions must be stuck together.
  SDValue InFlag;
  for (auto Reg : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, DL, Reg.first, Reg.second, InFlag);
    InFlag = Chain.getValue(1);
  }

  // Returns a chain & a flag for retval copy to use.
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  // Add argument registers to the end of the list so that they are known live
  // into the call.
  for (auto Reg : RegsToPass) {
    Ops.push_back(DAG.getRegister(Reg.first, Reg.second.getValueType()));
  }

  // Add a register mask operand representing the call-preserved registers.
  const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();
  const uint32_t *Mask =
      TRI->getCallPreservedMask(DAG.getMachineFunction(), CallConv);
  assert(Mask && "Missing call preserved mask for calling convention");
  Ops.push_back(DAG.getRegisterMask(Mask));

  if (InFlag.getNode())
    Ops.push_back(InFlag);

  if (isTailCall) {
    SmallVector<SDValue, 8> TailOps;
    TailOps.push_back(Chain);
    TailOps.push_back(Callee);
    TailOps.push_back(DAG.getConstant(0, DL, MVT::i16));

    for (auto Reg : RegsToPass)
      TailOps.push_back(
          DAG.getRegister(Reg.first, Reg.second.getValueType()));

    TailOps.push_back(DAG.getRegisterMask(Mask));

    if (InFlag.getNode())
      TailOps.push_back(InFlag);

    MF.getFrameInfo().setHasTailCall();
    return DAG.getNode(I8085ISD::TC_RETURN, DL, NodeTys, TailOps);
  }

  Chain = DAG.getNode(I8085ISD::CALL, DL, NodeTys, Ops);
  InFlag = Chain.getValue(1);

  // Create the CALLSEQ_END node.
  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, InFlag, DL);

  if (!Ins.empty()) {
    InFlag = Chain.getValue(1);
  }

  // Handle result values, copying them out of physregs into vregs that we
  // return.
  return LowerCallResult(Chain, InFlag, CallConv, isVarArg, Ins, DL, DAG,
                         InVals);
}

/// Lower the result values of a call into the
/// appropriate copies out of appropriate physical registers.
///
SDValue I8085TargetLowering::LowerCallResult(
    SDValue Chain, SDValue InFlag, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {

  if (Ins.size() == 1 && Ins[0].VT == MVT::i8) {
    SDValue V = DAG.getCopyFromReg(Chain, dl, I8085::A, MVT::i8, InFlag);
    Chain = V.getValue(1);
    InFlag = V.getValue(2);
    InVals.push_back(V.getValue(0));
    return Chain;
  }

  bool IsI64 =
      (!Ins.empty() && Ins[0].VT == MVT::i64) ||
      (Ins.size() == 2 && Ins[0].VT == MVT::i32 && Ins[1].VT == MVT::i32 &&
       (Ins[0].Flags.isSplit() || Ins[1].Flags.isSplit())) ||
      (Ins.size() == 2 &&
       Ins[0].VT == MVT::i32 && Ins[1].VT == MVT::i32);

  bool IsF32 =
      (Ins.size() == 1 && Ins[0].VT == MVT::f32);
  bool IsI32 =
      (Ins.size() == 1 && Ins[0].VT == MVT::i32) || IsF32;

  if (IsI64) {
    SDValue Lo = DAG.getCopyFromReg(Chain, dl, I8085::IAX, MVT::i32, InFlag);
    Chain = Lo.getValue(1);
    InFlag = Lo.getValue(2);
    SDValue Hi = DAG.getCopyFromReg(Chain, dl, I8085::IBX, MVT::i32, InFlag);
    Chain = Hi.getValue(1);
    InFlag = Hi.getValue(2);

    InVals.push_back(Lo.getValue(0));
    InVals.push_back(Hi.getValue(0));
    return Chain;
  }

  if (IsI32) {
    // Use PACK_CALL_RESULT_32 to atomically capture BC:DE into a GR32.
    // This avoids creating intermediate CopyFromReg + ZEXT + SHL + OR nodes
    // that the fast register allocator (O0) can miscode when there is
    // register pressure on HL.
    SDVTList VTs = DAG.getVTList(MVT::i32, MVT::Other, MVT::Glue);
    SDValue Ops[] = { Chain, InFlag };
    SDValue Val = DAG.getNode(I8085ISD::PACK_CALL_RESULT_32, dl, VTs, Ops);
    Chain = Val.getValue(1);
    InFlag = Val.getValue(2);

    if (IsF32)
      Val = DAG.getNode(ISD::BITCAST, dl, MVT::f32, Val);
    InVals.push_back(Val);
    return Chain;
  }

  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  // Handle runtime calling convs.

  CCInfo.AnalyzeCallResult(Ins, RetCC_I8085_BUILTIN);

  SmallVector<std::pair<int, unsigned>, 4> ResultMemLocs;

  // Copy all of the result registers out of their specified physreg.
  for (CCValAssign const &RVLoc : RVLocs) {
    if(RVLoc.isRegLoc()){
    SDValue Copy = DAG.getCopyFromReg(Chain, dl, RVLoc.getLocReg(),
                                      RVLoc.getValVT(), InFlag);
    Chain = Copy.getValue(1);
    InFlag = Copy.getValue(2);
    InVals.push_back(Copy.getValue(0));
    }
    else{
      assert(RVLoc.isMemLoc() && "Must be memory location.");
      ResultMemLocs.push_back(
          std::make_pair(RVLoc.getLocMemOffset(), InVals.size()));
      // Reserve space for this result.
      InVals.push_back(SDValue());
    }
  }

    // Copy results out of memory.
  SmallVector<SDValue, 4> MemOpChains;
  for (unsigned i = 0, e = ResultMemLocs.size(); i != e; ++i) {
    int Offset = ResultMemLocs[i].first;
    unsigned Index = ResultMemLocs[i].second;
    
    SDValue ptrConstant = DAG.getConstant(Offset,dl,MVT::i16);
    SDValue Load = DAG.getLoad(MVT::i32, dl, Chain, ptrConstant, MachinePointerInfo());
    InVals[Index] = Load;
    MemOpChains.push_back(Load.getValue(1));
  }

  // Transform all loads nodes into one single node because
  // all load nodes are independent of each other.
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains);

  return Chain;
}

//===----------------------------------------------------------------------===//
//               Return Value Calling Convention Implementation
//===----------------------------------------------------------------------===//

bool I8085TargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool isVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context) const {
  if (CallConv == CallingConv::I8085_BUILTIN) {
    for (const auto &Out : Outs) {
      if (Out.VT == MVT::f32)
        return true;
    }
    for (const auto &Out : Outs) {
      if (Out.VT == MVT::i64 || Out.Flags.isSplit())
        return true;
    }
    if (Outs.size() == 2 && Outs[0].VT == MVT::i32 &&
        Outs[1].VT == MVT::i32) {
      return true;
    }
    SmallVector<CCValAssign, 16> RVLocs;
    CCState CCInfo(CallConv, isVarArg, MF, RVLocs, Context);
    return CCInfo.CheckReturn(Outs, RetCC_I8085_BUILTIN);
  }

  if (Outs.size() > 1)
    return false;
  for (const auto &Out : Outs) {
    if (!(Out.VT.isInteger() || Out.VT == MVT::f32))
      return false;
  }

  unsigned TotalBytes = getTotalArgumentsSizeInBytes(Outs);
  // Only 4 return registers (B,C,D,E) available — max 4 bytes in registers.
  // i64 returns must use sret (indirect return via pointer).
  return TotalBytes <= 4;
}

SDValue
I8085TargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                               bool isVarArg,
                               const SmallVectorImpl<ISD::OutputArg> &Outs,
                               const SmallVectorImpl<SDValue> &OutVals,
                               const SDLoc &dl, SelectionDAG &DAG) const {
  // CCValAssign - represent the assignment of the return value to locations.
  SmallVector<CCValAssign, 16> RVLocs;
  auto DL = DAG.getDataLayout();

  // CCState - Info about the registers and stack slot.
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  MachineFunction &MF = DAG.getMachineFunction();

  if (MF.getFunction().getAttributes().hasFnAttr(Attribute::Naked)) {
    return Chain;
  }

  if (MF.getFunction().getReturnType()->isIntegerTy(8) && !OutVals.empty()) {
    SDValue Val = OutVals[0];
    if (Val.getValueType() != MVT::i8)
      Val = DAG.getNode(ISD::TRUNCATE, dl, MVT::i8, Val);
    SDValue Flag;
    Chain = DAG.getCopyToReg(Chain, dl, I8085::A, Val, Flag);
    SmallVector<SDValue, 4> RetOps;
    RetOps.push_back(Chain);
    RetOps.push_back(DAG.getRegister(I8085::A, MVT::i8));
    if (Flag.getNode())
      RetOps.push_back(Flag);
    return DAG.getNode(I8085ISD::RET_FLAG, dl, MVT::Other, RetOps);
  }

  bool IsI64 =
      (!Outs.empty() && Outs[0].VT == MVT::i64) ||
      (Outs.size() == 2 && Outs[0].VT == MVT::i32 && Outs[1].VT == MVT::i32 &&
       (Outs[0].Flags.isSplit() || Outs[1].Flags.isSplit())) ||
      (Outs.size() == 2 && OutVals.size() == 2 &&
       OutVals[0].getValueType() == MVT::i32 &&
       OutVals[1].getValueType() == MVT::i32);

  bool IsF32 =
      (Outs.size() == 1 && Outs[0].VT == MVT::f32) ||
      (OutVals.size() == 1 && OutVals[0].getValueType() == MVT::f32);
  bool IsI32 =
      (Outs.size() == 1 && Outs[0].VT == MVT::i32) ||
      (OutVals.size() == 1 && OutVals[0].getValueType() == MVT::i32) ||
      IsF32;

  if (IsI64) {
    SDValue Lo;
    SDValue Hi;
    if (Outs.size() == 2) {
      Lo = OutVals[0];
      Hi = OutVals[1];
    } else {
      splitI64Value(OutVals[0], Lo, Hi, DAG, dl);
    }

    SDValue Flag;
    Chain = DAG.getCopyToReg(Chain, dl, I8085::IAX, Lo, Flag);
    Flag = Chain.getValue(1);
    Chain = DAG.getCopyToReg(Chain, dl, I8085::IBX, Hi, Flag);
    Flag = Chain.getValue(1);

    SmallVector<SDValue, 4> RetOps;
    RetOps.push_back(Chain);
    RetOps.push_back(DAG.getRegister(I8085::IAX, MVT::i32));
    RetOps.push_back(DAG.getRegister(I8085::IBX, MVT::i32));
    if (Flag.getNode())
      RetOps.push_back(Flag);

    return DAG.getNode(I8085ISD::RET_FLAG, dl, MVT::Other, RetOps);
  }

  if (IsI32) {
    SDValue Val = OutVals[0];
    if (Val.getValueType() == MVT::f32)
      Val = DAG.getNode(ISD::BITCAST, dl, MVT::i32, Val);
    SDValue Lo = DAG.getNode(ISD::TRUNCATE, dl, MVT::i16, Val);
    // Use custom TRUNC32_HI node to extract high word directly without
    // a costly SRL by 16 that gets expanded to a 16-iteration loop.
    SDValue Hi = DAG.getNode(I8085ISD::TRUNC32_HI, dl, MVT::i16, Val);

    SDValue Flag;
    Chain = DAG.getCopyToReg(Chain, dl, I8085::BC, Lo, Flag);
    Flag = Chain.getValue(1);
    Chain = DAG.getCopyToReg(Chain, dl, I8085::DE, Hi, Flag);
    Flag = Chain.getValue(1);

    SmallVector<SDValue, 4> RetOps;
    RetOps.push_back(Chain);
    RetOps.push_back(DAG.getRegister(I8085::BC, MVT::i16));
    RetOps.push_back(DAG.getRegister(I8085::DE, MVT::i16));
    if (Flag.getNode())
      RetOps.push_back(Flag);

    return DAG.getNode(I8085ISD::RET_FLAG, dl, MVT::Other, RetOps);
  }

  CCInfo.AnalyzeReturn(Outs, RetCC_I8085_BUILTIN);

  MachineFrameInfo &MFI = MF.getFrameInfo();

  SDValue Flag;
  SmallVector<SDValue, 4> RetOps(1, Chain);
  // Copy the result values into the output registers.
  for (unsigned i = 0, e = RVLocs.size(); i != e; ++i) {
    CCValAssign &VA = RVLocs[i];
    if(VA.isRegLoc()){

      Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), OutVals[i], Flag);

      // Guarantee that all emitted copies are stuck together with flags.
      Flag = Chain.getValue(1);
      RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
    }
    else{
    int Offset = VA.getLocMemOffset();
    unsigned ObjSize = VA.getLocVT().getStoreSize();
    // Create the frame index object for the memory location.
    int FI = MFI.CreateFixedObject(ObjSize, Offset, false);

    // Create a SelectionDAG node corresponding to a store
    // to this memory location.
    SDValue FIN = DAG.getFrameIndex(FI, MVT::i16);
    if(ObjSize==8) FIN = DAG.getFrameIndex(FI, MVT::i8);


    Chain = DAG.getStore(Chain, dl, OutVals[i], FIN,MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI));


    unsigned RetOpc = I8085ISD::RET_FLAG;  
    RetOps[0] = Chain; 

    if (Flag.getNode()) {
          RetOps.push_back(Flag);
    }

    return DAG.getNode(RetOpc, dl, MVT::Other, RetOps); 
    }
  }

  unsigned RetOpc = I8085ISD::RET_FLAG;

  RetOps[0] = Chain; // Update chain.

  if (Flag.getNode()) {
    RetOps.push_back(Flag);
  }

  return DAG.getNode(RetOpc, dl, MVT::Other, RetOps);
}

MachineBasicBlock *I8085TargetLowering::insertShiftSet(MachineInstr &MI,
                                                  MachineBasicBlock *MBB) const {

  int Opc = MI.getOpcode();
  const I8085InstrInfo &TII = (const I8085InstrInfo &)*MI.getParent()
                                ->getParent()
                                ->getSubtarget()
                                .getInstrInfo();

  DebugLoc dl = MI.getDebugLoc();

  // To "insert" a SELECT instruction, we insert the diamond
  // control-flow pattern. The incoming instruction knows the
  // destination vreg to set, the condition code register to branch
  // on, the true/false values to select between, and a branch opcode
  // to use.

  MachineFunction *MF = MBB->getParent();
  
  const BasicBlock *LLVM_BB = MBB->getBasicBlock();
  MachineBasicBlock *FallThrough = MBB->getFallThrough();

  // If the current basic block falls through to another basic block,
  // we must insert an unconditional branch to the fallthrough destination
  // if we are to insert basic blocks at the prior fallthrough point.
  if (FallThrough != nullptr) {
    BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(FallThrough);
  }

  MachineBasicBlock *continuationMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *shiftLoopMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *checkMBB = MF->CreateMachineBasicBlock(LLVM_BB);

  MachineFunction::iterator I;
  for (I = MF->begin(); I != MF->end() && &(*I) != MBB; ++I)
    ;
  if (I != MF->end())
    ++I;
  
  MF->insert(I, shiftLoopMBB);
  MF->insert(I, checkMBB);
  MF->insert(I, continuationMBB);


  // Transfer remaining instructions and all successors of the current
  // block to the block which will contain the Phi node for the
  // select.
  continuationMBB->splice(continuationMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());

  continuationMBB->transferSuccessorsAndUpdatePHIs(MBB);

  MBB->addSuccessor(checkMBB);
  shiftLoopMBB->addSuccessor(checkMBB);
  checkMBB->addSuccessor(shiftLoopMBB);
  checkMBB->addSuccessor(continuationMBB);

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operandOne = MI.getOperand(1).getReg();
  unsigned operandTwo = MI.getOperand(2).getReg();

  // Avoid A/HL here because GR32 shift pseudos clobber A/HL as scratch.
  unsigned counterTempReg =
      MF->getRegInfo().createVirtualRegister(&I8085::GR8NoAHLRegClass);
  unsigned counterTempReg2 =
      MF->getRegInfo().createVirtualRegister(&I8085::GR8NoAHLRegClass);
  unsigned shiftCount =
      MF->getRegInfo().createVirtualRegister(&I8085::GR8NoAHLRegClass);

  unsigned tempHolderOne,tempHolderTwo;
  int rrOpcode;
  if(Opc==I8085::SHL_16 || Opc==I8085::SRA_16 || Opc==I8085::SRL_16){
      rrOpcode = I8085::RR_16;
      if(Opc==I8085::SHL_16) rrOpcode=I8085::RL_16;
      if(Opc==I8085::SRA_16) rrOpcode=I8085::ASR_16;
      tempHolderOne = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i16));
      tempHolderTwo = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i16));
  }

  if(Opc==I8085::SHL_8 || Opc==I8085::SRA_8 || Opc==I8085::SRL_8){
      rrOpcode = I8085::RR_8;
      if(Opc==I8085::SHL_8) rrOpcode=I8085::RL_8;
      if(Opc==I8085::SRA_8) rrOpcode=I8085::ASR_8;
      tempHolderOne = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));
      tempHolderTwo = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));
  }

  if(Opc==I8085::SHL_32 || Opc==I8085::SRA_32 || Opc==I8085::SRL_32){
      rrOpcode = I8085::RR_32;
      if(Opc==I8085::SHL_32) rrOpcode=I8085::RL_32;
      if(Opc==I8085::SRA_32) rrOpcode=I8085::ASR_32;
      tempHolderOne = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i32));
      tempHolderTwo = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i32));
  }

  //MBB:
  // Jump to loop MBB

  BuildMI(MBB, dl, TII.get(TargetOpcode::COPY), shiftCount)
      .addReg(operandTwo);

  BuildMI(MBB, dl, TII.get(I8085::JMP))
     .addMBB(checkMBB);

  // LoopBB:
  // tempHolderTwo = shift tempHolderOne

  BuildMI(shiftLoopMBB, dl, TII.get(rrOpcode))
        .addReg(tempHolderTwo, RegState::Define)
        .addReg(tempHolderOne);

  // checkMBB:
  // tempHolderOne = phi [%operandOne, BB], [%tempHolderTwo, LoopBB]
  // counterTempReg2 = phi [%operandTwo, BB], [%counterTempReg, LoopBB]
  // destReg  = phi [%operandOne, BB], [%tempHolderTwo,  LoopBB]
  // counterTempReg = counterTempReg2 - 1;
  // if (counterTempReg >= 0) goto shiftLoopMBB;
  
  BuildMI(checkMBB, dl, TII.get(I8085::PHI), tempHolderOne)
      .addReg(operandOne)
      .addMBB(MBB)
      .addReg(tempHolderTwo)
      .addMBB(shiftLoopMBB);

  BuildMI(checkMBB, dl, TII.get(I8085::PHI), counterTempReg2)
      .addReg(shiftCount)
      .addMBB(MBB)
      .addReg(counterTempReg)
      .addMBB(shiftLoopMBB);

  BuildMI(checkMBB, dl, TII.get(I8085::PHI), destReg)
      .addReg(operandOne)
      .addMBB(MBB)
      .addReg(tempHolderTwo)
      .addMBB(shiftLoopMBB);

  BuildMI(checkMBB, dl, TII.get(I8085::DCR))
      .addReg(counterTempReg,RegState::Define)
      .addReg(counterTempReg2);

  BuildMI(checkMBB, dl, TII.get(I8085::JP))
            .addMBB(shiftLoopMBB);      

  MI.eraseFromParent();
  return continuationMBB;
}

static MachineBasicBlock *insertSelectPseudo(MachineInstr &MI,
                                             MachineBasicBlock *MBB) {
  const I8085InstrInfo &TII = (const I8085InstrInfo &)*MI.getParent()
                                ->getParent()
                                ->getSubtarget()
                                .getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();

  MachineFunction *MF = MBB->getParent();
  const BasicBlock *LLVM_BB = MBB->getBasicBlock();
  MachineBasicBlock *FallThrough = MBB->getFallThrough();

  if (FallThrough != nullptr) {
    BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(FallThrough);
  }

  MachineBasicBlock *trueMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *falseMBB = MF->CreateMachineBasicBlock(LLVM_BB);

  MachineFunction::iterator I;
  for (I = MF->begin(); I != MF->end() && &(*I) != MBB; ++I)
    ;
  if (I != MF->end())
    ++I;
  MF->insert(I, trueMBB);
  MF->insert(I, falseMBB);

  trueMBB->splice(trueMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());
  trueMBB->transferSuccessorsAndUpdatePHIs(MBB);

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned condReg = MI.getOperand(1).getReg();
  unsigned trueReg = MI.getOperand(2).getReg();
  unsigned falseReg = MI.getOperand(3).getReg();

  // Materialize the condition into A immediately before branching so
  // intervening GR32 pseudo expansions cannot clobber it.
  BuildMI(MBB, dl, TII.get(I8085::MOV))
      .addReg(I8085::A, RegState::Define)
      .addReg(condReg);
  BuildMI(MBB, dl, TII.get(I8085::ORI)).addImm(0);
  BuildMI(MBB, dl, TII.get(I8085::JNZ)).addMBB(trueMBB);
  BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(falseMBB);

  MBB->addSuccessor(falseMBB);
  MBB->addSuccessor(trueMBB);

  BuildMI(falseMBB, dl, TII.get(I8085::JMP)).addMBB(trueMBB);
  falseMBB->addSuccessor(trueMBB);

  BuildMI(*trueMBB, trueMBB->begin(), dl, TII.get(I8085::PHI), destReg)
      .addReg(trueReg)
      .addMBB(MBB)
      .addReg(falseReg)
      .addMBB(falseMBB);

  MI.eraseFromParent();
  return trueMBB;
}


MachineBasicBlock *I8085TargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                               MachineBasicBlock *MBB) const {
  int Opc = MI.getOpcode();

  // Pseudo shift instructions with a non constant shift amount are expanded
  // into a loop.
  switch (Opc) {
  case I8085::SET_NE_8:
  case I8085::SET_EQ_8:
  case I8085::SET_UGT_8:
  case I8085::SET_ULT_8:
  case I8085::SET_UGE_8:
  case I8085::SET_ULE_8:
    return insertCond8Set(MI, MBB);
  case I8085::BR_CC_SLT_8:
  case I8085::BR_CC_SGE_8:
    return insertBrCCSigned8(MI, MBB);
  case I8085::BR_CC_EQ_16:
  case I8085::BR_CC_NE_16:
  case I8085::BR_CC_ULT_16:
  case I8085::BR_CC_UGE_16:
  case I8085::BR_CC_SLT_16:
  case I8085::BR_CC_SGE_16:
    return insertBrCC16(MI, MBB);
  case I8085::SET_GT_8:
  case I8085::SET_LT_8:
  case I8085::SET_GE_8:
  case I8085::SET_LE_8:
    return insertSigned8Cond(MI,MBB);
  case I8085::SET_DIFF_SIGN_GT_8:
  case I8085::SET_DIFF_SIGN_LT_8:
  case I8085::SET_DIFF_SIGN_GE_8:
  case I8085::SET_DIFF_SIGN_LE_8:
    return insertDifferentSigned8Cond(MI,MBB);

  case I8085::SET_NE_16:
  case I8085::SET_EQ_16:
    return insertEqualityCond16Set(MI,MBB);

  case I8085::SET_UGT_16:
  case I8085::SET_ULT_16:
  case I8085::SET_UGE_16:
  case I8085::SET_ULE_16:
    return insertCond16Set(MI, MBB); 

  case I8085::SET_GT_16:
  case I8085::SET_LT_16:
  case I8085::SET_GE_16:
  case I8085::SET_LE_16:
    return insertSignedCond16Set(MI, MBB);   

  case I8085::SET_DIFF_SIGN_LT_16:
  case I8085::SET_DIFF_SIGN_GT_16:
    return insertDifferentSignedCond16Set(MI, MBB); 
  

  case I8085::SET_NE_32:
  case I8085::SET_EQ_32:
    return insertEqualityCond32Set(MI,MBB);
  

  case I8085::SET_UGT_32:
  case I8085::SET_ULT_32:
  case I8085::SET_UGE_32:
  case I8085::SET_ULE_32:
    return insertCond32Set(MI, MBB); 
  
  case I8085::SET_GT_32:
  case I8085::SET_LT_32:
  case I8085::SET_GE_32:
  case I8085::SET_LE_32:
    return insertSignedCond32Set(MI, MBB); 


  case I8085::SET_DIFF_SIGN_LT_32:
  case I8085::SET_DIFF_SIGN_GT_32:
    return insertDifferentSignedCond32Set(MI, MBB);   

  case I8085::SELECT_8:
  case I8085::SELECT_16:
  case I8085::SELECT_32:
    return insertSelectPseudo(MI, MBB);

  case I8085::SHL_8:
  case I8085::SRA_8:
  case I8085::SRL_8:
  case I8085::SHL_16:
  case I8085::SRA_16:
  case I8085::SRL_16:
  case I8085::SHL_32:
  case I8085::SRA_32:
  case I8085::SRL_32:
    return insertShiftSet(MI, MBB);      
  }

  assert( false &&
         "Unexpected instr type to insert");
  return MBB;
}


Register I8085TargetLowering::getRegisterByName(const char *RegName, LLT VT,
                                              const MachineFunction &MF) const {
  Register Reg;

  if (VT == LLT::scalar(8)) {
    Reg = StringSwitch<unsigned>(RegName)
              .Default(0);
  } else {
    Reg = StringSwitch<unsigned>(RegName)
              .Default(0);
  }

  if (Reg)
    return Reg;

  report_fatal_error(
      Twine("Invalid register name \"" + StringRef(RegName) + "\"."));
}

} 
