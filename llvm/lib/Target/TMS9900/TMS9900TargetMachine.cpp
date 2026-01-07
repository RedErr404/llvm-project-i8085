//===-- TMS9900TargetMachine.cpp - Define TargetMachine for TMS9900 -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the TMS9900 specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#include "TMS9900TargetMachine.h"
#include "TMS9900.h"
#include "TMS9900MachineFunctionInfo.h"
#include "TMS9900TargetObjectFile.h"
#include "TargetInfo/TMS9900TargetInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeTMS9900Target() {
  // Register the target
  RegisterTargetMachine<TMS9900TargetMachine> X(getTheTMS9900Target());
}

static std::string computeDataLayout(const Triple &TT) {
  // TMS9900 Data Layout:
  // - Big-endian (E)
  // - 16-bit pointers (p:16:16)
  // - 16-bit integers as native (i16:16:16)
  // - 8-bit aligned bytes (i8:8:8)
  // - Stack alignment: 16-bit (S16)
  // - Native integer width: 16-bit (n16)
  return "E-p:16:16-i8:8:8-i16:16:16-i32:16:32-n16-S16";
}

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  // TMS9900 typically uses static (absolute) addressing
  return RM.value_or(Reloc::Static);
}

TMS9900TargetMachine::TMS9900TargetMachine(const Target &T, const Triple &TT,
                                             StringRef CPU, StringRef FS,
                                             const TargetOptions &Options,
                                             std::optional<Reloc::Model> RM,
                                             std::optional<CodeModel::Model> CM,
                                             CodeGenOptLevel OL, bool JIT)
    : LLVMTargetMachine(T, computeDataLayout(TT), TT,
                        CPU.empty() ? "tms9900" : CPU, FS, Options,
                        getEffectiveRelocModel(RM),
                        CM.value_or(CodeModel::Small), OL),
      TLOF(std::make_unique<TMS9900TargetObjectFile>()),
      Subtarget(TT, CPU.empty() ? std::string("tms9900") : std::string(CPU),
                std::string(FS), *this) {
  initAsmInfo();
}

namespace {
class TMS9900PassConfig : public TargetPassConfig {
public:
  TMS9900PassConfig(TMS9900TargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  TMS9900TargetMachine &getTMS9900TargetMachine() const {
    return getTM<TMS9900TargetMachine>();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  void addPreEmitPass() override;
};
} // end anonymous namespace

TargetPassConfig *TMS9900TargetMachine::createPassConfig(PassManagerBase &PM) {
  return new TMS9900PassConfig(*this, PM);
}

MachineFunctionInfo *TMS9900TargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return TMS9900MachineFunctionInfo::create<TMS9900MachineFunctionInfo>(
      Allocator, F, STI);
}

void TMS9900PassConfig::addIRPasses() {
  // Expand atomic operations to regular load/store/RMW.
  // TMS9900 is single-core with no caches, so atomics are trivially correct.
  addPass(createAtomicExpandPass());

  TargetPassConfig::addIRPasses();
}

bool TMS9900PassConfig::addInstSelector() {
  // Install an instruction selector
  addPass(createTMS9900ISelDag(getTMS9900TargetMachine(), getOptLevel()));
  return false;
}

void TMS9900PassConfig::addPreEmitPass() {
  addPass(&BranchRelaxationPassID);
}
