//===-- TMS9900RegisterInfo.cpp - TMS9900 Register Information ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the TMS9900 implementation of the TargetRegisterInfo
// class.
//
//===----------------------------------------------------------------------===//

#include "TMS9900RegisterInfo.h"
#include "TMS9900.h"
#include "TMS9900Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/MC/MCInstrDesc.h"

using namespace llvm;

#define DEBUG_TYPE "tms9900-reg-info"

#define GET_REGINFO_TARGET_DESC
#include "TMS9900GenRegisterInfo.inc"

TMS9900RegisterInfo::TMS9900RegisterInfo(const TMS9900Subtarget &STI)
    : TMS9900GenRegisterInfo(TMS9900::R11), Subtarget(STI) {}

const MCPhysReg *
TMS9900RegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  // R13, R14, R15 are callee-saved
  // R10 (SP) and R11 (LR) are handled specially
  static const MCPhysReg CalleeSavedRegs[] = {
    TMS9900::R13, TMS9900::R14, TMS9900::R15, 0
  };
  return CalleeSavedRegs;
}

const uint32_t *
TMS9900RegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                           CallingConv::ID) const {
  return CSR_TMS9900_RegMask;
}

BitVector TMS9900RegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());

  // R10 is the stack pointer
  Reserved.set(TMS9900::R10);

  // R11 is the link register (return address)
  Reserved.set(TMS9900::R11);

  // R12 is typically reserved for CRU base address
  // But could be made allocatable if not doing I/O
  Reserved.set(TMS9900::R12);

  // Internal registers are not allocatable
  Reserved.set(TMS9900::PC);
  Reserved.set(TMS9900::WP);
  Reserved.set(TMS9900::ST);

  return Reserved;
}

bool TMS9900RegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator MI,
                                               int SPAdj,
                                               unsigned FIOperandNum,
                                               RegScavenger *RS) const {
  MachineInstr &MI_ref = *MI;
  MachineBasicBlock &MBB = *MI_ref.getParent();
  MachineFunction &MF = *MBB.getParent();
  const TMS9900Subtarget &Subtarget = MF.getSubtarget<TMS9900Subtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  DebugLoc DL = MI_ref.getDebugLoc();

  int FrameIndex = MI_ref.getOperand(FIOperandNum).getIndex();
  int64_t Offset = MFI.getObjectOffset(FrameIndex) + SPAdj;

  // Add the offset from the stack pointer (R10)
  // Stack objects are at negative offsets from the original SP,
  // but we need to compute from the current SP (after prologue allocation)
  Offset += MFI.getStackSize();

  unsigned Opc = MI_ref.getOpcode();

  // Handle MOV_FI_Load and MOV_FI_Store with indexed addressing
  // These instructions have a memri operand: (register, immediate)
  // The frame index is in operand FIOperandNum, and the offset is in FIOperandNum+1
  if (Opc == TMS9900::MOV_FI_Load || Opc == TMS9900::MOV_FI_Store) {
    // Replace frame index with R10 (stack pointer)
    MI_ref.getOperand(FIOperandNum).ChangeToRegister(TMS9900::R10, false);

    // Add the computed offset to the existing immediate offset
    MachineOperand &OffsetOp = MI_ref.getOperand(FIOperandNum + 1);
    int64_t ExistingOffset = OffsetOp.getImm();
    OffsetOp.setImm(Offset + ExistingOffset);

    return false;
  }

  // For other instructions that just need a register with the address
  // (like legacy code or other uses of frame indices)

  int TiedOp = MI_ref.getDesc().getOperandConstraint(FIOperandNum,
                                                     MCOI::TIED_TO);
  bool TiedToDef = (TiedOp != -1);

  // If offset is 0 and the operand is not tied, simply replace with R10.
  if (Offset == 0 && !TiedToDef) {
    MI_ref.getOperand(FIOperandNum).ChangeToRegister(TMS9900::R10, false);
    return false;
  }

  if (TiedToDef) {
    MachineOperand &DefOp = MI_ref.getOperand(TiedOp);
    if (DefOp.isReg()) {
      Register BaseReg = DefOp.getReg();

      // Compute BaseReg = R10 + Offset so the tied def uses the same register.
      BuildMI(MBB, MI, DL, TII.get(TMS9900::MOVrr), BaseReg)
          .addReg(TMS9900::R10);
      if (Offset != 0) {
        BuildMI(MBB, MI, DL, TII.get(TMS9900::AI), BaseReg)
            .addReg(BaseReg)
            .addImm(Offset);
      }

      MI_ref.getOperand(FIOperandNum).ChangeToRegister(BaseReg, false);
      return false;
    }
  }

  // For non-zero offsets, compute the effective address into a scratch register.
  // Since we can't create new virtual registers at this stage,
  // we use R9 as a scratch register (with caveats) or scavenge.
  // For now, use register scavenger if available, otherwise use R9.

  Register ScratchReg;
  if (RS) {
    ScratchReg = RS->scavengeRegisterBackwards(TMS9900::GR16RegClass, MI,
                                                /*RestoreAfter=*/false, SPAdj,
                                                /*AllowSpill=*/false);
  }

  if (!ScratchReg) {
    // Use R9 as a scratch register (this may not be safe in all cases)
    // A more robust solution would require spilling
    ScratchReg = TMS9900::R9;
  }

  // Compute address: ScratchReg = R10 + Offset
  // MOV R10, ScratchReg
  BuildMI(MBB, MI, DL, TII.get(TMS9900::MOVrr), ScratchReg)
      .addReg(TMS9900::R10);

  // AI ScratchReg, Offset
  BuildMI(MBB, MI, DL, TII.get(TMS9900::AI), ScratchReg)
      .addReg(ScratchReg)
      .addImm(Offset);

  // Replace the frame index operand with the scratch register
  MI_ref.getOperand(FIOperandNum).ChangeToRegister(ScratchReg, false);

  return false;
}

Register TMS9900RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  // We use R10 as the stack pointer / frame pointer
  return TMS9900::R10;
}
