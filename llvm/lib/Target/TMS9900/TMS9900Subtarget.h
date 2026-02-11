//===-- TMS9900Subtarget.h - Define Subtarget for the TMS9900 ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the TMS9900 specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_TMS9900_TMS9900SUBTARGET_H
#define LLVM_LIB_TARGET_TMS9900_TMS9900SUBTARGET_H

#include "TMS9900FrameLowering.h"
#include "TMS9900ISelLowering.h"
#include "TMS9900InstrInfo.h"
#include "TMS9900RegisterInfo.h"
#include "llvm/CodeGen/SelectionDAGTargetInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DataLayout.h"
#include <string>

#define GET_SUBTARGETINFO_HEADER
#include "TMS9900GenSubtargetInfo.inc"

namespace llvm {
class StringRef;

class TMS9900Subtarget : public TMS9900GenSubtargetInfo {
  virtual void anchor();

  TMS9900RegisterInfo RegInfo;
  TMS9900InstrInfo InstrInfo;
  TMS9900FrameLowering FrameLowering;
  TMS9900TargetLowering TLInfo;
  SelectionDAGTargetInfo TSInfo;

  // Subtarget feature bits (populated by ParseSubtargetFeatures).
  bool ReserveCRU = false;

public:
  TMS9900Subtarget(const Triple &TT, StringRef CPU, StringRef FS,
                   const TargetMachine &TM);

  /// ParseSubtargetFeatures - Parses features string setting specified
  /// subtarget options.
  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);

  const TMS9900InstrInfo *getInstrInfo() const override { return &InstrInfo; }
  const TMS9900FrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }
  const TMS9900TargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }
  const TMS9900RegisterInfo *getRegisterInfo() const override {
    return &RegInfo;
  }
  const SelectionDAGTargetInfo *getSelectionDAGInfo() const override {
    return &TSInfo;
  }

  /// Returns true if R12 should be reserved for CRU base address.
  bool reserveCRU() const { return ReserveCRU; }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_TMS9900_TMS9900SUBTARGET_H
