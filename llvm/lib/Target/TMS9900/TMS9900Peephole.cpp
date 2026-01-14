//===-- TMS9900Peephole.cpp - TMS9900 peephole opts ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Simple post-isel peephole optimizations:
//  - AI +/-1/2 -> INC/DEC/INCT/DECT
//  - LI 0 -> CLR
//  - XOR r,r -> CLR
//  - AI 0 removed when status flags are dead
//
//===----------------------------------------------------------------------===//

#include "TMS9900.h"
#include "TMS9900InstrInfo.h"
#include "TMS9900Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"

using namespace llvm;

namespace {
static bool tryFoldPostInc(MachineInstr &IncMI,
                           const TargetInstrInfo *TII,
                           const TargetRegisterInfo *TRI) {
  int64_t IncAmount = 0;
  unsigned IncOpc = IncMI.getOpcode();
  if (IncOpc == TMS9900::AI) {
    if (!IncMI.getOperand(2).isImm())
      return false;
    IncAmount = IncMI.getOperand(2).getImm();
  } else if (IncOpc == TMS9900::INCr) {
    IncAmount = 1;
  } else if (IncOpc == TMS9900::INCTr) {
    IncAmount = 2;
  } else {
    return false;
  }

  if (IncAmount != 1 && IncAmount != 2)
    return false;

  MachineInstr *Prev = IncMI.getPrevNode();
  while (Prev && Prev->isDebugInstr())
    Prev = Prev->getPrevNode();
  if (!Prev)
    return false;

  unsigned NewOpc = 0;
  bool IsLoad = false;
  bool IsByte = false;
  Register AddrReg;
  Register ValueReg;

  switch (Prev->getOpcode()) {
  default:
    return false;
  case TMS9900::MOVim:
    IsLoad = true;
    IsByte = false;
    NewOpc = TMS9900::MOVpim;
    ValueReg = Prev->getOperand(0).getReg();
    AddrReg = Prev->getOperand(1).getReg();
    break;
  case TMS9900::MOVBim:
    IsLoad = true;
    IsByte = true;
    NewOpc = TMS9900::MOVBpim;
    ValueReg = Prev->getOperand(0).getReg();
    AddrReg = Prev->getOperand(1).getReg();
    break;
  case TMS9900::MOVmi:
    IsLoad = false;
    IsByte = false;
    NewOpc = TMS9900::MOVmpi;
    AddrReg = Prev->getOperand(0).getReg();
    ValueReg = Prev->getOperand(1).getReg();
    break;
  case TMS9900::MOVBmi:
    IsLoad = false;
    IsByte = true;
    NewOpc = TMS9900::MOVBmpi;
    AddrReg = Prev->getOperand(0).getReg();
    ValueReg = Prev->getOperand(1).getReg();
    break;
  }

  if (IsByte && IncAmount != 1)
    return false;
  if (!IsByte && IncAmount != 2)
    return false;

  if (IncMI.getOperand(0).getReg() != AddrReg ||
      IncMI.getOperand(1).getReg() != AddrReg) {
    return false;
  }

  MachineBasicBlock &MBB = *IncMI.getParent();
  DebugLoc DL = Prev->getDebugLoc();
  bool DeadAddr = IncMI.getOperand(0).isDead();
  bool KillAddr = IncMI.getOperand(1).isKill();
  bool DeadST = Prev->registerDefIsDead(TMS9900::ST, TRI);

  MachineInstrBuilder MIB = BuildMI(MBB, Prev, DL, TII->get(NewOpc));
  if (IsLoad) {
    bool DeadValue = Prev->getOperand(0).isDead();
    MIB.addReg(ValueReg, RegState::Define | (DeadValue ? RegState::Dead : 0));
    MIB.addReg(AddrReg, RegState::Define | (DeadAddr ? RegState::Dead : 0));
    MIB.addReg(AddrReg, KillAddr ? RegState::Kill : 0);
  } else {
    MIB.addReg(AddrReg, RegState::Define | (DeadAddr ? RegState::Dead : 0));
    MIB.addReg(AddrReg, KillAddr ? RegState::Kill : 0);
    unsigned ValFlags = Prev->getOperand(1).isKill() ? RegState::Kill : 0;
    MIB.addReg(ValueReg, ValFlags);
  }

  if (int STIdx = MIB->findRegisterDefOperandIdx(TMS9900::ST, true);
      STIdx != -1 && DeadST) {
    MIB->getOperand(STIdx).setIsDead();
  }

  Prev->eraseFromParent();
  IncMI.eraseFromParent();
  return true;
}

class TMS9900PeepholePass : public MachineFunctionPass {
public:
  static char ID;
  TMS9900PeepholePass() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "TMS9900 peephole opts"; }

  bool runOnMachineFunction(MachineFunction &MF) override {
    const auto *TII = MF.getSubtarget<TMS9900Subtarget>().getInstrInfo();
    const auto *TRI = MF.getSubtarget().getRegisterInfo();
    bool Changed = false;

    for (MachineBasicBlock &MBB : MF) {
      for (auto It = MBB.instr_begin(); It != MBB.instr_end(); ) {
        MachineInstr &MI = *It++;
        unsigned Opc = MI.getOpcode();

        if (tryFoldPostInc(MI, TII, TRI)) {
          Changed = true;
          continue;
        }

        if (Opc == TMS9900::AI) {
          if (!MI.getOperand(2).isImm())
            continue;
          int64_t Imm = MI.getOperand(2).getImm();
          int16_t Imm16 = static_cast<int16_t>(Imm);
          unsigned NewOpc = 0;

          switch (Imm16) {
          case 1:
            NewOpc = TMS9900::INCr;
            break;
          case 2:
            NewOpc = TMS9900::INCTr;
            break;
          case -1:
            NewOpc = TMS9900::DECr;
            break;
          case -2:
            NewOpc = TMS9900::DECTr;
            break;
          case 0:
            if (MI.registerDefIsDead(TMS9900::ST, TRI)) {
              MI.eraseFromParent();
              Changed = true;
            }
            continue;
          default:
            continue;
          }

          Register Reg = MI.getOperand(0).getReg();
          bool DeadDef = MI.getOperand(0).isDead();
          bool KillUse = MI.getOperand(1).isKill();
          DebugLoc DL = MI.getDebugLoc();
          MachineInstrBuilder MIB = BuildMI(MBB, MI, DL, TII->get(NewOpc));
          MIB.addReg(Reg, RegState::Define | (DeadDef ? RegState::Dead : 0));
          MIB.addReg(Reg, KillUse ? RegState::Kill : 0);

          if (int STIdx = MIB->findRegisterDefOperandIdx(TMS9900::ST, true);
              STIdx != -1 &&
              MI.registerDefIsDead(TMS9900::ST, TRI)) {
            MIB->getOperand(STIdx).setIsDead();
          }

          MI.eraseFromParent();
          Changed = true;
          continue;
        }

        if (Opc == TMS9900::LI) {
          if (!MI.getOperand(1).isImm())
            continue;
          int64_t Imm = MI.getOperand(1).getImm();
          int16_t Imm16 = static_cast<int16_t>(Imm);
          if (Imm16 != 0)
            continue;

          Register Reg = MI.getOperand(0).getReg();
          bool DeadDef = MI.getOperand(0).isDead();
          DebugLoc DL = MI.getDebugLoc();
          MachineInstrBuilder MIB =
              BuildMI(MBB, MI, DL, TII->get(TMS9900::CLRr));
          MIB.addReg(Reg, RegState::Define | (DeadDef ? RegState::Dead : 0));

          if (int STIdx = MIB->findRegisterDefOperandIdx(TMS9900::ST, true);
              STIdx != -1 &&
              MI.registerDefIsDead(TMS9900::ST, TRI)) {
            MIB->getOperand(STIdx).setIsDead();
          }

          MI.eraseFromParent();
          Changed = true;
          continue;
        }

        if (Opc == TMS9900::XORrr) {
          Register Dst = MI.getOperand(0).getReg();
          Register Src1 = MI.getOperand(1).getReg();
          Register Src2 = MI.getOperand(2).getReg();
          if (Dst != Src1 || Src1 != Src2)
            continue;

          bool DeadDef = MI.getOperand(0).isDead();
          DebugLoc DL = MI.getDebugLoc();
          MachineInstrBuilder MIB =
              BuildMI(MBB, MI, DL, TII->get(TMS9900::CLRr));
          MIB.addReg(Dst, RegState::Define | (DeadDef ? RegState::Dead : 0));

          if (int STIdx = MIB->findRegisterDefOperandIdx(TMS9900::ST, true);
              STIdx != -1 &&
              MI.registerDefIsDead(TMS9900::ST, TRI)) {
            MIB->getOperand(STIdx).setIsDead();
          }

          MI.eraseFromParent();
          Changed = true;
        }
      }
    }

    return Changed;
  }
};
} // namespace

char TMS9900PeepholePass::ID = 0;

INITIALIZE_PASS(TMS9900PeepholePass, "tms9900-peephole",
                "TMS9900 peephole opts", false, false)

FunctionPass *llvm::createTMS9900PeepholePass() {
  return new TMS9900PeepholePass();
}
