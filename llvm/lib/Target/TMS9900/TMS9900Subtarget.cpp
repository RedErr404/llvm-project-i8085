//===-- TMS9900Subtarget.cpp - TMS9900 Subtarget Information --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TMS9900 specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "TMS9900Subtarget.h"
#include "TMS9900.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "tms9900-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "TMS9900GenSubtargetInfo.inc"

void TMS9900Subtarget::anchor() {}

TMS9900Subtarget::TMS9900Subtarget(const Triple &TT, StringRef CPU,
                                     StringRef FS, const TargetMachine &TM)
    : TMS9900GenSubtargetInfo(TT, CPU, /*TuneCPU*/ CPU, FS),
      RegInfo(*this),
      InstrInfo(*this),
      FrameLowering(*this),
      TLInfo(TM, *this) {
  ParseSubtargetFeatures(CPU, CPU, FS);
}
