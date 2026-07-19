//===-- I8085InstrInfo.cpp - I8085 Instruction Information --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the I8085 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "I8085InstrInfo.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"

#include "I8085.h"
#include "I8085MachineFunctionInfo.h"
#include "I8085RegisterInfo.h"
#include "I8085TargetMachine.h"
#include "MCTargetDesc/I8085MCTargetDesc.h"

#include <iostream>

#define GET_INSTRINFO_CTOR_DTOR
#include "I8085GenInstrInfo.inc"

namespace llvm {

I8085InstrInfo::I8085InstrInfo()
    : I8085GenInstrInfo(I8085::ADJCALLSTACKDOWN, I8085::ADJCALLSTACKUP), RI() {}

static bool getPairRegs(MCRegister Reg, unsigned &LowReg, unsigned &HighReg) {
  switch (Reg) {
  case I8085::BC:
    LowReg = I8085::C;
    HighReg = I8085::B;
    return true;
  case I8085::DE:
    LowReg = I8085::E;
    HighReg = I8085::D;
    return true;
  case I8085::HL:
    LowReg = I8085::L;
    HighReg = I8085::H;
    return true;
  default:
    return false;
  }
}

void I8085InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MI,
                               const DebugLoc &DL, MCRegister DestReg,
                               MCRegister SrcReg, bool KillSrc) const {
  unsigned Opc;


  if (I8085::GR8RegClass.contains(DestReg, SrcReg)) {
      Opc = I8085::MOV;

      BuildMI(MBB, MI, DL, get(Opc), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
  } else if(I8085::GR16RegClass.contains(DestReg, SrcReg)) {

  unsigned destLow, destHigh;
  unsigned srcLow, srcHigh;

  if (DestReg == I8085::SP) {
    if (SrcReg == I8085::HL) {
      BuildMI(MBB, MI, DL, get(I8085::SPHL));
      return;
    }
    if (!getPairRegs(SrcReg, srcLow, srcHigh))
      return;
    BuildMI(MBB, MI, DL, get(I8085::MOV), I8085::H)
        .addReg(srcHigh, getKillRegState(KillSrc));
    BuildMI(MBB, MI, DL, get(I8085::MOV), I8085::L)
        .addReg(srcLow, getKillRegState(KillSrc));
    BuildMI(MBB, MI, DL, get(I8085::SPHL));
    return;
  }

  if (SrcReg == I8085::SP) {
    BuildMI(MBB, MI, DL, get(I8085::LXI), I8085::HL).addImm(0);
    BuildMI(MBB, MI, DL, get(I8085::DAD)).addReg(I8085::SP);
    if (DestReg == I8085::HL)
      return;
    if (!getPairRegs(DestReg, destLow, destHigh))
      return;
    BuildMI(MBB, MI, DL, get(I8085::MOV), destHigh)
        .addReg(I8085::H);
    BuildMI(MBB, MI, DL, get(I8085::MOV), destLow)
        .addReg(I8085::L);
    return;
  }

  if (!getPairRegs(DestReg, destLow, destHigh))
    return;
  if (!getPairRegs(SrcReg, srcLow, srcHigh))
    return;

  Opc = I8085::MOV;
  BuildMI(MBB, MI, DL, get(Opc), destHigh)
        .addReg(srcHigh, getKillRegState(KillSrc));
  BuildMI(MBB, MI, DL, get(Opc), destLow)
        .addReg(srcLow, getKillRegState(KillSrc));    
  } else if (I8085::GR32RegClass.contains(DestReg, SrcReg)) {
    Opc = I8085::MOV_32;
    BuildMI(MBB, MI, DL, get(Opc), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
  } else if (I8085::GR32RegClass.contains(DestReg) &&
             I8085::GR8RegClass.contains(SrcReg)) {
    BuildMI(MBB, MI, DL, get(I8085::ZEXT8TO32), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
  } else if (I8085::GR32RegClass.contains(DestReg) &&
             I8085::GR16RegClass.contains(SrcReg)) {
    BuildMI(MBB, MI, DL, get(I8085::AEXT16TO32), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
  } else if (I8085::GR8RegClass.contains(DestReg) &&
             I8085::GR32RegClass.contains(SrcReg)) {
    BuildMI(MBB, MI, DL, get(I8085::TRUNC32TO8), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
  } else if (I8085::GR16RegClass.contains(DestReg) &&
             I8085::GR32RegClass.contains(SrcReg)) {
    BuildMI(MBB, MI, DL, get(I8085::TRUNC32TO16), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
  }
}

Register I8085InstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                           int &FrameIndex) const {
  switch (MI.getOpcode()) {
  case I8085::LOAD_8_WITH_ADDR:
  case I8085::LOAD_16_WITH_ADDR:
  case I8085::LOAD_32_WITH_ADDR: { 
    if (MI.getOperand(1).isFI() && MI.getOperand(2).isImm() &&
        MI.getOperand(2).getImm() == 0) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  }
  default:
    break;
  }

  return 0;
}

Register I8085InstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                          int &FrameIndex) const {
  switch (MI.getOpcode()) {
  case I8085::STORE_8:
  case I8085::STORE_16: 
  case I8085::STORE_32: {
    if (MI.getOperand(0).isFI() && MI.getOperand(1).isImm() &&
        MI.getOperand(1).getImm() == 0) {
      FrameIndex = MI.getOperand(0).getIndex();
      return MI.getOperand(2).getReg();
    }
    break;
  }
  default:
    break;
  }

  return 0;
}

void I8085InstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MI,
                                       Register SrcReg, bool isKill,
                                       int FrameIndex,
                                       const TargetRegisterClass *RC,
                                       const TargetRegisterInfo *TRI,
                                       Register VReg) const {
  MachineFunction &MF = *MBB.getParent();
  I8085MachineFunctionInfo *AFI = MF.getInfo<I8085MachineFunctionInfo>();

  AFI->setHasSpills(true);

  DebugLoc DL;
  if (MI != MBB.end()) {
    DL = MI->getDebugLoc();
  }

  const MachineFrameInfo &MFI = MF.getFrameInfo();

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIndex),
      MachineMemOperand::MOStore, MFI.getObjectSize(FrameIndex),
      MFI.getObjectAlign(FrameIndex));

  unsigned Opcode = 0;
  if (TRI->isTypeLegalForClass(*RC, MVT::i8)) {
    Opcode = I8085::STORE_8;
  } else if (TRI->isTypeLegalForClass(*RC, MVT::i16)) {
    Opcode = I8085::STORE_16;
  } else if (TRI->isTypeLegalForClass(*RC, MVT::i32)) {
    Opcode = I8085::STORE_32;
  } else {
    llvm_unreachable("Cannot store this register into a stack slot!");
  }

  MachineRegisterInfo &MRI = MF.getRegInfo();
  if (Opcode == I8085::STORE_8 && RC != &I8085::GR8NoHLRegClass) {
    // STORE_8 uses HL as the address register, so H/L cannot be the source.
    if (SrcReg.isVirtual() &&
        MRI.constrainRegClass(SrcReg, &I8085::GR8NoHLRegClass)) {
      // Narrowed to a safe register class.
    } else if (!I8085::GR8NoHLRegClass.contains(SrcReg)) {
      Register Tmp = MRI.createVirtualRegister(&I8085::GR8NoHLRegClass);
      BuildMI(MBB, MI, DL, get(TargetOpcode::COPY), Tmp)
          .addReg(SrcReg, getKillRegState(isKill));
      SrcReg = Tmp;
      isKill = true;
    }
  }

  BuildMI(MBB, MI, DL, get(Opcode))
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .addReg(SrcReg, getKillRegState(isKill))
      .addMemOperand(MMO);
}

void I8085InstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MI,
                                        Register DestReg, int FrameIndex,
                                        const TargetRegisterClass *RC,
                                        const TargetRegisterInfo *TRI,
                                        Register VReg) const {
  DebugLoc DL;
  if (MI != MBB.end()) {
    DL = MI->getDebugLoc();
  }

  MachineFunction &MF = *MBB.getParent();
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIndex),
      MachineMemOperand::MOLoad, MFI.getObjectSize(FrameIndex),
      MFI.getObjectAlign(FrameIndex));

  unsigned Opcode = 0;
  if (TRI->isTypeLegalForClass(*RC, MVT::i8)) {
    Opcode = I8085::LOAD_8_WITH_ADDR;
  } else if (TRI->isTypeLegalForClass(*RC, MVT::i16)) {
    Opcode = I8085::LOAD_16_WITH_ADDR;
  } else if (TRI->isTypeLegalForClass(*RC, MVT::i32)) {
    Opcode = I8085::LOAD_32_WITH_ADDR;
  } else {
    llvm_unreachable("Cannot load this register from a stack slot!");
  }

  // LOAD_8_WITH_ADDR uses HL internally as the address register, so loading
  // directly into H or L is unsafe: the loaded value would be clobbered when
  // the expansion writes to HL for address computation.  Constrain the
  // destination to GR8NoHL when possible.
  if (Opcode == I8085::LOAD_8_WITH_ADDR && DestReg.isVirtual()) {
    MachineRegisterInfo &MRI = MF.getRegInfo();
    MRI.constrainRegClass(DestReg, &I8085::GR8NoHLRegClass);
  }

  BuildMI(MBB, MI, DL, get(Opcode), DestReg)
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .addMemOperand(MMO);
}


unsigned I8085InstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  unsigned Opcode = MI.getOpcode();

  switch (Opcode) {
  case I8085::TCRETURN:
    return 3;
  case I8085::TCRETURN_INDIRECT:
    return 3; // MOV H,B ; MOV L,C ; PCHL
  // A regular instruction
  default: {
    const MCInstrDesc &Desc = get(Opcode);
    unsigned Size = Desc.getSize();
    // Pseudos have Size=0 in tablegen. Return a conservative estimate
    // for any pass that queries size before expansion. Most pseudos expand
    // to 5-20 bytes; 16 is a reasonable upper bound for estimation.
    if (Size == 0 && Desc.isPseudo())
      return 16;
    return Size;
  }
  case TargetOpcode::EH_LABEL:
  case TargetOpcode::IMPLICIT_DEF:
  case TargetOpcode::KILL:
  case TargetOpcode::DBG_VALUE:
    return 0;
  }
}

static bool isCondBranchOpcode(unsigned Opc) {
  switch (Opc) {
  case I8085::JZ:
  case I8085::JNZ:
  case I8085::JC:
  case I8085::JNC:
  case I8085::JP:
  case I8085::JM:
  case I8085::JPE:
  case I8085::JPO:
    return true;
  default:
    return false;
  }
}

static unsigned getOppositeBranchOpcode(unsigned Opc) {
  switch (Opc) {
  case I8085::JZ:
    return I8085::JNZ;
  case I8085::JNZ:
    return I8085::JZ;
  case I8085::JC:
    return I8085::JNC;
  case I8085::JNC:
    return I8085::JC;
  case I8085::JP:
    return I8085::JM;
  case I8085::JM:
    return I8085::JP;
  case I8085::JPE:
    return I8085::JPO;
  case I8085::JPO:
    return I8085::JPE;
  default:
    llvm_unreachable("unexpected branch opcode!");
  }
}

bool I8085InstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                   MachineBasicBlock *&TBB,
                                   MachineBasicBlock *&FBB,
                                   SmallVectorImpl<MachineOperand> &Cond,
                                   bool AllowModify) const {
  TBB = nullptr;
  FBB = nullptr;
  Cond.clear();

  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;

    if (!isUnpredicatedTerminator(*I))
      break;

    if (!I->isBranch())
      return true;

    unsigned Opc = I->getOpcode();
    if (Opc == I8085::JMP) {
      if (!AllowModify) {
        TBB = I->getOperand(0).getMBB();
        continue;
      }

      MBB.erase(std::next(I), MBB.end());
      Cond.clear();
      FBB = nullptr;

      if (MBB.isLayoutSuccessor(I->getOperand(0).getMBB())) {
        TBB = nullptr;
        I->eraseFromParent();
        I = MBB.end();
        continue;
      }

      TBB = I->getOperand(0).getMBB();
      continue;
    }

    if (!isCondBranchOpcode(Opc))
      return true;

    if (Cond.empty()) {
      FBB = TBB;
      TBB = I->getOperand(0).getMBB();
      Cond.push_back(MachineOperand::CreateImm(Opc));
      continue;
    }

    assert(Cond.size() == 1);
    assert(TBB);

    if (TBB != I->getOperand(0).getMBB())
      return true;

    unsigned OldOpc = Cond[0].getImm();
    if (OldOpc == Opc)
      continue;

    return true;
  }

  return false;
}

unsigned I8085InstrInfo::insertBranch(MachineBasicBlock &MBB,
                                      MachineBasicBlock *TBB,
                                      MachineBasicBlock *FBB,
                                      ArrayRef<MachineOperand> Cond,
                                      const DebugLoc &DL,
                                      int *BytesAdded) const {
  assert(TBB && "insertBranch must not be told to insert a fallthrough");
  assert((Cond.size() == 1 || Cond.empty()) &&
         "I8085 branch conditions have one component!");

  if (BytesAdded)
    *BytesAdded = 0;

  if (Cond.empty()) {
    assert(!FBB && "Unconditional branch with multiple successors!");
    auto &MI = *BuildMI(&MBB, DL, get(I8085::JMP)).addMBB(TBB);
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(MI);
    return 1;
  }

  unsigned Opc = Cond[0].getImm();
  assert(isCondBranchOpcode(Opc) && "Invalid conditional branch opcode");
  auto &CondMI = *BuildMI(&MBB, DL, get(Opc)).addMBB(TBB);
  if (BytesAdded)
    *BytesAdded += getInstSizeInBytes(CondMI);

  if (FBB) {
    auto &MI = *BuildMI(&MBB, DL, get(I8085::JMP)).addMBB(FBB);
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(MI);
    return 2;
  }

  return 1;
}

unsigned I8085InstrInfo::removeBranch(MachineBasicBlock &MBB,
                                      int *BytesRemoved) const {
  if (BytesRemoved)
    *BytesRemoved = 0;

  unsigned Count = 0;
  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;

    unsigned Opc = I->getOpcode();
    if (Opc != I8085::JMP && !isCondBranchOpcode(Opc))
      break;

    if (BytesRemoved)
      *BytesRemoved += getInstSizeInBytes(*I);
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }

  return Count;
}

bool I8085InstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 1 && "Invalid I8085 branch condition");
  unsigned Opc = Cond[0].getImm();
  Cond[0].setImm(getOppositeBranchOpcode(Opc));
  return false;
}

MachineBasicBlock *I8085InstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("unexpected branch opcode!");
  case I8085::JMP:
  case I8085::JZ:
  case I8085::JP:
  case I8085::JM:
  case I8085::JNZ:
  case I8085::JC:
  case I8085::JNC:
  case I8085::JPE:
  case I8085::JPO:
    return MI.getOperand(0).getMBB();
  }
}


bool I8085InstrInfo::isBranchOffsetInRange(unsigned BranchOp,
                                         int64_t BrOffset) const {

  switch (BranchOp) {
  default:
    llvm_unreachable("unexpected opcode!");
  case I8085::JMP:
  case I8085::JZ:
  case I8085::JP:
  case I8085::JM:
  case I8085::JNZ:
  case I8085::JC:
  case I8085::JNC:
  case I8085::JPE:
  case I8085::JPO:
    return true;
  }
}

} // end of namespace llvm
