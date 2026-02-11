//===-- TMS9900FrameLowering.cpp - TMS9900 Frame Lowering -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the TMS9900 implementation of TargetFrameLowering class.
//
// TMS9900 Stack Frame Layout:
//
// The TMS9900 has no hardware stack, so we implement a software stack using R10
// as the stack pointer. The stack grows downward (high to low addresses).
//
// Function Prologue:
//   1. Save return address (R11) to stack: DECT R10, MOV R11,*R10
//   2. Save callee-saved registers if needed
//   3. Allocate local stack space: AI R10,-framesize
//
// Function Epilogue:
//   1. Deallocate local stack space: AI R10,framesize
//   2. Restore callee-saved registers
//   3. Restore return address: MOV *R10+,R11
//   4. Return: B *R11
//
//===----------------------------------------------------------------------===//

#include "TMS9900FrameLowering.h"
#include "TMS9900.h"
#include "TMS9900InstrInfo.h"
#include "TMS9900Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

TMS9900FrameLowering::TMS9900FrameLowering(const TMS9900Subtarget &STI)
    : TargetFrameLowering(StackGrowsDown,
                          /*StackAlignment=*/Align(4),
                          /*LocalAreaOffset=*/0) {}
// NOTE: Stack alignment is 4 bytes (not 2) to match LLVM's type legalizer
// assumptions. When splitting i32 loads/stores, LLVM uses OR-instead-of-ADD
// to compute the low-word address (e.g., ORI Rx,2 instead of AI Rx,2).
// This optimization assumes bit 1 of the base address is zero, which requires
// 4-byte stack alignment. With 2-byte alignment, non-4-byte-aligned stack
// addresses cause ORI to be a no-op, reading the high word twice.

bool TMS9900FrameLowering::hasFP(const MachineFunction &MF) const {
  // We don't use a separate frame pointer - just the stack pointer (R10)
  // Could implement FP later if needed for variable-length arrays
  return false;
}

void TMS9900FrameLowering::emitPrologue(MachineFunction &MF,
                                         MachineBasicBlock &MBB) const {
  const Function &F = MF.getFunction();

  // Naked functions have no prologue
  if (F.hasFnAttribute(Attribute::Naked))
    return;

  // Interrupt handlers have no prologue - the CPU has already set up the
  // workspace and saved context to R13-R15
  if (F.hasFnAttribute("interrupt"))
    return;

  MachineBasicBlock::iterator MBBI = MBB.begin();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const TMS9900Subtarget &STI = MF.getSubtarget<TMS9900Subtarget>();
  const TMS9900InstrInfo &TII = *STI.getInstrInfo();
  DebugLoc DL;

  if (MBBI != MBB.end())
    DL = MBBI->getDebugLoc();

  // Get the stack size (already aligned by processFunctionBeforeFrameFinalized)
  uint64_t StackSize = MFI.getStackSize();

  // If we have a stack frame, we need to:
  // 1. Save the return address (R11) - done by caller or here
  // 2. Adjust the stack pointer

  // For non-leaf functions, save R11 (return address)
  // DECT R10        ; SP -= 2
  // MOV R11,*R10    ; Push R11
  if (!MFI.hasCalls() && StackSize == 0) {
    // Leaf function with no locals - no prologue needed
    return;
  }

  // Push return address for non-leaf functions
  if (MFI.hasCalls()) {
    // DECT R10 - decrement stack pointer
    BuildMI(MBB, MBBI, DL, TII.get(TMS9900::DECTr), TMS9900::R10)
        .addReg(TMS9900::R10);
    // MOV R11,*R10 - store R11 at address in R10
    BuildMI(MBB, MBBI, DL, TII.get(TMS9900::MOVmi))
        .addReg(TMS9900::R10)   // pointer (destination address)
        .addReg(TMS9900::R11);  // source value

    // CFI: CFA is now at R10+2 (we pushed 2 bytes)
    unsigned CFIIndex = MF.addFrameInst(
        MCCFIInstruction::cfiDefCfaOffset(nullptr, 2));
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);

    // CFI: R11 (return address, DWARF reg 11) is saved at CFA-2
    CFIIndex = MF.addFrameInst(
        MCCFIInstruction::createOffset(nullptr, 11, -2));
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);
  }

  // Allocate stack space.
  // For non-leaf functions, the DECT above subtracted 2 from SP, breaking
  // 4-byte alignment (SP is now 4k+2). To restore 4-byte alignment, we
  // add 2 extra bytes of padding to the AI allocation. This makes the total
  // prologue displacement = 2 (DECT) + StackSize + 2 (pad) = StackSize + 4,
  // which is 4-byte aligned (since StackSize is always 4-aligned).
  // The extra 2 bytes are accounted for in eliminateFrameIndex.
  uint64_t AllocSize = StackSize;
  if (MFI.hasCalls() && StackSize > 0)
    AllocSize += 2;  // alignment padding after DECT

  if (AllocSize > 0) {
    // AI R10,-AllocSize
    BuildMI(MBB, MBBI, DL, TII.get(TMS9900::AI), TMS9900::R10)
        .addReg(TMS9900::R10)
        .addImm(-static_cast<int64_t>(AllocSize));

    // CFI: CFA offset grows by AllocSize
    unsigned CFIIndex = MF.addFrameInst(
        MCCFIInstruction::cfiDefCfaOffset(nullptr, AllocSize + (MFI.hasCalls() ? 2 : 0)));
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);
  }
}

void TMS9900FrameLowering::emitEpilogue(MachineFunction &MF,
                                         MachineBasicBlock &MBB) const {
  const Function &F = MF.getFunction();

  // Naked functions have no epilogue
  if (F.hasFnAttribute(Attribute::Naked))
    return;

  // Interrupt handlers have no epilogue - RTWP restores everything
  if (F.hasFnAttribute("interrupt"))
    return;

  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const TMS9900Subtarget &STI = MF.getSubtarget<TMS9900Subtarget>();
  const TMS9900InstrInfo &TII = *STI.getInstrInfo();
  DebugLoc DL;

  if (MBBI != MBB.end())
    DL = MBBI->getDebugLoc();

  uint64_t StackSize = MFI.getStackSize();

  if (!MFI.hasCalls() && StackSize == 0) {
    // Leaf function with no locals - no epilogue needed
    return;
  }

  // Deallocate stack space (must match the allocation in emitPrologue)
  uint64_t DeallocSize = StackSize;
  if (MFI.hasCalls() && StackSize > 0)
    DeallocSize += 2;  // alignment padding (matches prologue)

  if (DeallocSize > 0) {
    // AI R10,DeallocSize
    BuildMI(MBB, MBBI, DL, TII.get(TMS9900::AI), TMS9900::R10)
        .addReg(TMS9900::R10)
        .addImm(DeallocSize);
  }

  // Restore return address for non-leaf functions
  if (MFI.hasCalls()) {
    // MOV *R10+,R11 - load R11 from address in R10, then R10 += 2
    BuildMI(MBB, MBBI, DL, TII.get(TMS9900::MOVpim))
        .addDef(TMS9900::R11)   // $rd - loaded value
        .addDef(TMS9900::R10)   // $rs_wb - incremented pointer (tied to $rs)
        .addUse(TMS9900::R10);  // $rs - original pointer
  }

  // CFI: Restore CFA to entry state
  {
    unsigned CFIIndex = MF.addFrameInst(
        MCCFIInstruction::cfiDefCfaOffset(nullptr, 0));
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);
  }

  // The actual return (B *R11) is handled by the RET pseudo
}

MachineBasicBlock::iterator TMS9900FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  const TMS9900Subtarget &STI = MF.getSubtarget<TMS9900Subtarget>();
  const TMS9900InstrInfo &TII = *STI.getInstrInfo();

  if (hasReservedCallFrame(MF))
    return MBB.erase(I);

  MachineInstr &MI = *I;
  DebugLoc DL = MI.getDebugLoc();
  int64_t Amount = MI.getOperand(0).getImm();

  if (Amount != 0) {
    // Adjust stack pointer
    if (MI.getOpcode() == TMS9900::ADJCALLSTACKDOWN) {
      // Allocate space: AI R10,-Amount
      BuildMI(MBB, I, DL, TII.get(TMS9900::AI), TMS9900::R10)
          .addReg(TMS9900::R10)
          .addImm(-Amount);
    } else {
      assert(MI.getOpcode() == TMS9900::ADJCALLSTACKUP);
      // Deallocate space: AI R10,Amount
      BuildMI(MBB, I, DL, TII.get(TMS9900::AI), TMS9900::R10)
          .addReg(TMS9900::R10)
          .addImm(Amount);
    }
  }

  return MBB.erase(I);
}

void TMS9900FrameLowering::determineCalleeSaves(MachineFunction &MF,
                                                 BitVector &SavedRegs,
                                                 RegScavenger *RS) const {
  const Function &F = MF.getFunction();

  // Naked functions don't spill callee-saved registers
  if (F.hasFnAttribute(Attribute::Naked))
    return;

  // Interrupt handlers don't need to save callee-saved registers in the
  // traditional sense - they have their own workspace
  if (F.hasFnAttribute("interrupt"))
    return;

  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);

  // R13, R14, R15 are callee-saved
  // They will be saved if used
}

void TMS9900FrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
  // Nothing to do here -- alignment padding is handled in emitPrologue/
  // emitEpilogue and accounted for in eliminateFrameIndex.
}
