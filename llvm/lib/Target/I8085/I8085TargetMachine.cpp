//===-- I8085TargetMachine.cpp - Define TargetMachine for I8085 ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the I8085 specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#include "I8085TargetMachine.h"

#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"

#include "I8085.h"
#include "I8085MachineFunctionInfo.h"
#include "I8085TargetObjectFile.h"
#include "I8085TargetTransformInfo.h"
#include "MCTargetDesc/I8085MCTargetDesc.h"
#include "TargetInfo/I8085TargetInfo.h"

namespace llvm {

static const char *I8085DataLayout =
    "e-p:16:8-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8-a:8";

/// Processes a CPU name.
static StringRef getCPU(StringRef CPU) {
  if (CPU.empty() || CPU == "generic") {
    return "i8085";
  }

  return CPU;
}

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

I8085TargetMachine::I8085TargetMachine(const Target &T, const Triple &TT,
                                    StringRef CPU, StringRef FS,
                                    const TargetOptions &Options,
                                    std::optional<Reloc::Model> RM,
                                    std::optional<CodeModel::Model> CM,
                                    CodeGenOptLevel OL, bool JIT)
    : LLVMTargetMachine(T, I8085DataLayout, TT, getCPU(CPU), FS, Options,
                        getEffectiveRelocModel(RM),
                        getEffectiveCodeModel(CM, CodeModel::Small), OL),
      SubTarget(TT, std::string(getCPU(CPU)),
                (TT.str().find("+undoc") != std::string::npos &&
                 FS.find("+undoc") == std::string::npos)
                    ? (FS + "+undoc").str()
                    : FS.str(),
                *this) {
  this->TLOF = std::make_unique<I8085TargetObjectFile>();
  initAsmInfo();
}

namespace {
/// I8085 Code Generator Pass Configuration Options.
class I8085PassConfig : public TargetPassConfig {
public:
  I8085PassConfig(I8085TargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  I8085TargetMachine &getI8085TargetMachine() const {
    return getTM<I8085TargetMachine>();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  void addPreRegAlloc() override;
  void addMachineLateOptimization() override;
  void addPreSched2() override;
  void addPreEmitPass() override;
};
} // namespace

TargetPassConfig *I8085TargetMachine::createPassConfig(PassManagerBase &PM) {
  return new I8085PassConfig(*this, PM);
}

/// Annotate size-optimized functions with a conservative inline threshold.
/// The LLVM inliner's IR-level cost model (InstrCost=5 per IR instruction)
/// underestimates machine code expansion on the 8-bit i8085 where each IR
/// instruction expands to 4-10 machine instructions but a CALL is only 3 bytes.
/// Setting threshold=0 (effective threshold=1 due to max(1,Threshold) clamp)
/// ensures only net-beneficial inlines (cost <= 0) occur under -Os/-Oz.
struct I8085AnnotateInlineThresholdPass
    : public PassInfoMixin<I8085AnnotateInlineThresholdPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
    bool Changed = false;
    for (Function &F : M) {
      if (F.isDeclaration())
        continue;
      if (F.hasOptSize() || F.hasMinSize()) {
        F.addFnAttr("function-inline-threshold", "0");
        Changed = true;
      }
    }
    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }
};

void I8085TargetMachine::registerPassBuilderCallbacks(PassBuilder &PB) {
  PB.registerPipelineEarlySimplificationEPCallback(
      [](ModulePassManager &PM, OptimizationLevel Level) {
        if (Level == OptimizationLevel::O0)
          return;
        PM.addPass(I8085AnnotateInlineThresholdPass());
      });
}

void I8085PassConfig::addIRPasses() {
  // Expand instructions like
  //   %result = shl i32 %n, %amount
  // to a loop so that library calls are avoided.

  TargetPassConfig::addIRPasses();

  // Convert select+add patterns to branches for types > 16 bits.
  // This undoes InstCombine's transformation of branch-based conditional
  // addition into branchless select+add arithmetic, which is catastrophically
  // expensive on the 8085 where i32 operations cost ~200 instructions.
  // Must run after standard IR passes (including InstCombine) so the select
  // pattern is stable.
  addPass(createI8085SelectToBranchPass());
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeI8085Target() {
  // Register the target.
  RegisterTargetMachine<I8085TargetMachine> X(getTheI8085Target());

  auto &PR = *PassRegistry::getPassRegistry();
  initializeI8085ExpandPseudoPass(PR);
  initializeI8085ExpandPseudo32Pass(PR);
  initializeI8085StoreRegClassPass(PR);
  initializeI8085ExpandCopiesPass(PR);
  initializeI8085PeepholePass(PR);
  initializeI8085FrameAnalyzerPass(PR);
  initializeI8085DAGToDAGISelLegacyPass(PR);
  initializeI8085SelectToBranchPass(PR);
}

const I8085Subtarget *I8085TargetMachine::getSubtargetImpl() const {
  return &SubTarget;
}

const I8085Subtarget *I8085TargetMachine::getSubtargetImpl(const Function &) const {
  return &SubTarget;
}

TargetTransformInfo
I8085TargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(I8085TTIImpl(this, F));
}

//===----------------------------------------------------------------------===//
// Pass Pipeline Configuration
//===----------------------------------------------------------------------===//

bool I8085PassConfig::addInstSelector() {
  // Install an instruction selector.
  addPass(createI8085ISelDag(getI8085TargetMachine(), getOptLevel()));
  // Create the frame analyzer pass used by the PEI pass.
  addPass(createI8085FrameAnalyzerPass());

  return false;
}

void I8085PassConfig::addPreRegAlloc() {
  addPass(createI8085StoreRegClassPass());
}

void I8085PassConfig::addMachineLateOptimization() {
  // Override the default addMachineLateOptimization to omit the late Machine
  // Copy Propagation pass.  The i8085 GR32 pseudo instructions (LOAD_32,
  // STORE_32, etc.) declare implicit-def $hl because their expansion uses HL
  // for stack access.  The expansion pass (in addPreSched2) detects when HL is
  // live and wraps the expansion in PUSH H / POP H to preserve it.  However,
  // Machine Copy Propagation runs BEFORE pseudo expansion and sees the
  // implicit-def $hl as a clobber, incorrectly eliminating COPY instructions
  // that set HL to a value needed by successor blocks.  This causes miscompiles
  // at -O1 (e.g. lz_stress2 LZ77 roundtrip tests).
  //
  // We keep MachineLateInstrsCleanup, BranchFolder, and TailDuplicate but
  // skip the second MachineCopyPropagation pass.
  addPass(&MachineLateInstrsCleanupID);
  addPass(&BranchFolderPassID);
  if (!TM->requiresStructuredCFG())
    addPass(&TailDuplicateID);
  // MachineCopyPropagationID intentionally omitted — see above.
}

void I8085PassConfig::addPreSched2() {
  // ExpandPseudo32 must run before ExpandPseudo so it can see GROW_STACK_BY
  // and SHRINK_STACK_BY instructions for correct SP offset tracking.
  addPass(createI8085ExpandPseudo32Pass());
  addPass(createI8085ExpandPseudoPass());
  addPass(createI8085ExpandCopiesPass());
  addPass(createI8085PeepholePass());
  addPass(createI8085HLTrackingPass());
}

void I8085PassConfig::addPreEmitPass() {
  // The i8085 uses absolute 16-bit addresses for all branches (JMP, JZ, JNZ,
  // etc.), so every branch can reach every address in the 64KB address space.
  // Branch relaxation is not needed and is therefore not added here.
}

MachineFunctionInfo *I8085TargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return I8085MachineFunctionInfo::create<I8085MachineFunctionInfo>(Allocator, F,
                                                                STI);
}


} // end of namespace llvm
