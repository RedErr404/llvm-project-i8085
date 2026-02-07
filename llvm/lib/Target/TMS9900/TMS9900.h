//===-- TMS9900.h - Top-level interface for TMS9900 -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the
// LLVM TMS9900 back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_TMS9900_TMS9900_H
#define LLVM_LIB_TARGET_TMS9900_TMS9900_H

#include "llvm/Target/TargetMachine.h"

// Defines symbolic names for TMS9900 registers.
#define GET_REGINFO_ENUM
#include "TMS9900GenRegisterInfo.inc"

// Defines symbolic names for the TMS9900 instructions.
#define GET_INSTRINFO_ENUM
#include "TMS9900GenInstrInfo.inc"

// Defines symbolic names for TMS9900 subtarget features.
#define GET_SUBTARGETINFO_ENUM
#include "TMS9900GenSubtargetInfo.inc"

namespace llvm {

class TMS9900TargetMachine;
class FunctionPass;
class PassRegistry;

FunctionPass *createTMS9900ISelDag(TMS9900TargetMachine &TM,
                                    CodeGenOptLevel OptLevel);
FunctionPass *createTMS9900PeepholePass();
FunctionPass *createTMS9900LongBranchPass();
void initializeTMS9900PeepholePassPass(PassRegistry &);
void initializeTMS9900LongBranchPassPass(PassRegistry &);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_TMS9900_TMS9900_H
