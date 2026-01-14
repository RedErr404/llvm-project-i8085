//===-- TMS9900LongBranch.cpp - Long branch fixes ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Expand long-range signed JGT/JLT branches into JEQ/J{LT,GT} + B_sym so
// the long branch can be taken without an extra trampoline block.
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
class TMS9900LongBranchPass : public MachineFunctionPass {
public:
  static char ID;
  TMS9900LongBranchPass() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "TMS9900 long-range signed branch expansion";
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    const auto *TII = MF.getSubtarget<TMS9900Subtarget>().getInstrInfo();
    SmallVector<MachineBasicBlock *, 4> DeadBlocks;
    bool Changed = false;

    for (MachineBasicBlock &MBB : MF) {
      auto LastIt = MBB.getLastNonDebugInstr();
      if (LastIt == MBB.instr_end())
        continue;

      MachineInstr *CondMI = nullptr;
      MachineInstr *JmpMI = nullptr;
      if (LastIt->getOpcode() == TMS9900::JMP) {
        JmpMI = &*LastIt;
        CondMI = JmpMI->getPrevNode();
        while (CondMI && CondMI->isDebugInstr())
          CondMI = CondMI->getPrevNode();
      } else {
        CondMI = &*LastIt;
      }

      if (!CondMI)
        continue;

      unsigned CondOpc = CondMI->getOpcode();
      if (CondOpc != TMS9900::JGT && CondOpc != TMS9900::JLT)
        continue;

      if (!CondMI->getOperand(0).isMBB())
        continue;
      MachineBasicBlock *LongBB = CondMI->getOperand(0).getMBB();
      if (!LongBB || LongBB->pred_size() != 1 ||
          *LongBB->pred_begin() != &MBB)
        continue;

      auto LongIt = LongBB->getFirstNonDebugInstr();
      if (LongIt == LongBB->instr_end() ||
          LongIt->getOpcode() != TMS9900::B_sym ||
          !LongIt->getOperand(0).isMBB())
        continue;
      auto NextIt = LongIt;
      ++NextIt;
      while (NextIt != LongBB->end() && NextIt->isDebugInstr())
        ++NextIt;
      if (NextIt != LongBB->end())
        continue;

      MachineBasicBlock *TargetBB = LongIt->getOperand(0).getMBB();
      if (!TargetBB)
        continue;

      MachineBasicBlock *FalseBB = nullptr;
      if (JmpMI) {
        if (!JmpMI->getOperand(0).isMBB())
          continue;
        FalseBB = JmpMI->getOperand(0).getMBB();
      } else {
        FalseBB = MBB.getNextNode();
      }
      if (!FalseBB || FalseBB == LongBB)
        continue;

      DebugLoc DL = CondMI->getDebugLoc();
      unsigned OppOpc = (CondOpc == TMS9900::JGT) ? TMS9900::JLT : TMS9900::JGT;

      BuildMI(MBB, CondMI, DL, TII->get(TMS9900::JEQ)).addMBB(FalseBB);
      BuildMI(MBB, CondMI, DL, TII->get(OppOpc)).addMBB(FalseBB);
      BuildMI(MBB, CondMI, DL, TII->get(TMS9900::B_sym)).addMBB(TargetBB);

      CondMI->eraseFromParent();
      if (JmpMI)
        JmpMI->eraseFromParent();

      if (MBB.isSuccessor(LongBB))
        MBB.removeSuccessor(LongBB);
      if (!MBB.isSuccessor(FalseBB))
        MBB.addSuccessor(FalseBB);
      if (!MBB.isSuccessor(TargetBB))
        MBB.addSuccessor(TargetBB);

      if (LongBB->pred_empty())
        DeadBlocks.push_back(LongBB);

      Changed = true;
    }

    for (MachineBasicBlock *BB : DeadBlocks)
      BB->eraseFromParent();

    return Changed;
  }
};
} // namespace

char TMS9900LongBranchPass::ID = 0;

INITIALIZE_PASS(TMS9900LongBranchPass, "tms9900-long-branch",
                "TMS9900 long-range signed branch expansion", false, false)

FunctionPass *llvm::createTMS9900LongBranchPass() {
  return new TMS9900LongBranchPass();
}
