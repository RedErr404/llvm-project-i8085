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
#include "llvm/InitializePasses.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<bool> DisableTMS9900Peephole(
    "tms9900-disable-peephole",
    cl::desc("Disable TMS9900 peephole optimizations"),
    cl::init(false), cl::Hidden);

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeTMS9900Target() {
  // Register the target
  RegisterTargetMachine<TMS9900TargetMachine> X(getTheTMS9900Target());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeTMS9900PeepholePassPass(PR);
  initializeTMS9900LongBranchPassPass(PR);
}

static std::string computeDataLayout(const Triple &TT) {
  // TMS9900 Data Layout:
  // - Big-endian (E)
  // - 16-bit pointers (p:16:16)
  // - 16-bit integers as native (i16:16:16)
  // - 8-bit aligned bytes (i8:8:8)
  // - 32-bit integers: 16-bit ABI / 32-bit preferred alignment (i32:16:32)
  // - Stack alignment: 32-bit (S32)
  //   NOTE: Stack MUST be 4-byte aligned (S32, matching FrameLowering Align(4))
  //   because LLVM's type legalizer uses OR-instead-of-ADD to compute the
  //   low-word address of split i32 values (ORI Rx,2 instead of AI Rx,2).
  //   This optimization assumes bit 1 of the base address is zero, which
  //   requires 4-byte alignment. With S16/Align(2), addresses like 0xFAFE
  //   cause ORI to be a no-op, reading the high word twice.
  // - Native integer width: 16-bit (n16)
  return "E-p:16:16-i8:8:8-i16:16:16-i32:16:32-n16-S32";
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
  if (!DisableTMS9900Peephole)
    addPass(createTMS9900PeepholePass());
  addPass(&BranchRelaxationPassID);
  addPass(createTMS9900LongBranchPass());
}
