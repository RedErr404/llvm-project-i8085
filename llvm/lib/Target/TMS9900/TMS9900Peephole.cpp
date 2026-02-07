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
//  - LI -1 -> SETO
//  - XOR r,r -> CLR
//  - MOV Rx,Rx (self-move) -> delete (also when ST live and prior set flags)
//  - CI Rx,0 -> delete when prior instruction already set flags
//  - CI Rx,0 -> MOV Rx,Rx when CI cannot be fully eliminated (2 bytes smaller)
//  - Crr Rx,Ry -> delete when one operand is provably zero and prior set flags
//  - ANDI Rx,0xFF00 -> delete when next use of Rx is MOVB source
//  - SRL Rx,8 + ANDI Rx,0x00FF -> SWPB Rx + ANDI Rx,0x00FF
//  - SLA Rx,8 + ANDI Rx,0xFF00 -> SWPB Rx + ANDI Rx,0xFF00
//  - Dead first load when consecutive loads define same register
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

        // Redundant load elimination: if two consecutive instructions both
        // define the same register and the first value is never read, delete
        // the first instruction.  This runs early, before opcode-specific
        // transforms (e.g. LI->CLR) that would skip non-matching immediates.
        if (Opc == TMS9900::MOVam || Opc == TMS9900::MOVxm ||
            Opc == TMS9900::MOVim || Opc == TMS9900::MOV_FI_Load ||
            Opc == TMS9900::LI) {
          MachineInstr *Next = MI.getNextNode();
          while (Next && Next->isDebugInstr())
            Next = Next->getNextNode();
          if (Next) {
            Register DefReg = MI.getOperand(0).getReg();
            if (Next->modifiesRegister(DefReg, TRI) &&
                !Next->readsRegister(DefReg, TRI) &&
                MI.registerDefIsDead(TMS9900::ST, TRI)) {
              MI.eraseFromParent();
              Changed = true;
              continue;
            }
          }
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

          unsigned NewOpc;
          if (Imm16 == 0)
            NewOpc = TMS9900::CLRr;
          else if (Imm16 == -1)
            NewOpc = TMS9900::SETOr;
          else
            continue;

          Register Reg = MI.getOperand(0).getReg();
          bool DeadDef = MI.getOperand(0).isDead();
          DebugLoc DL = MI.getDebugLoc();
          MachineInstrBuilder MIB =
              BuildMI(MBB, MI, DL, TII->get(NewOpc));
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
          continue;
        }

        // MOV Rx,Rx (self-move) -> delete.
        // Case 1: ST is dead -- the self-move is a pure no-op.
        // Case 2: ST is live -- the self-move is used as a zero-test to
        //   set flags. If the immediately preceding instruction already
        //   defines Rx as operand 0 AND sets ST, the flags are already
        //   correct (all TMS9900 ALU ops set EQ/LGT/AGT identically to
        //   MOV for the same value), so the self-move is redundant.
        //   Same safety constraints as CI Rx,0 elimination.
        if (Opc == TMS9900::MOVrr) {
          Register Dst = MI.getOperand(0).getReg();
          Register Src = MI.getOperand(1).getReg();
          if (Dst != Src)
            continue;

          // Case 1: ST dead -- always safe to delete.
          if (MI.registerDefIsDead(TMS9900::ST, TRI)) {
            MI.eraseFromParent();
            Changed = true;
            continue;
          }

          // Case 2: ST live -- check that the preceding instruction
          // already set the same flags on Dst.
          MachineInstr *Prev = MI.getPrevNode();
          while (Prev && Prev->isDebugInstr())
            Prev = Prev->getPrevNode();
          if (!Prev)
            continue;

          // Safety: skip calls, branches, returns.
          if (Prev->isCall() || Prev->isBranch() || Prev->isReturn())
            continue;
          // Preceding instruction must set ST.
          if (!Prev->modifiesRegister(TMS9900::ST, TRI))
            continue;
          // Operand 0 must be a def of Dst (primary result = flags
          // reflect Dst's value, not a secondary def).
          if (Prev->getNumOperands() == 0 ||
              !Prev->getOperand(0).isReg() ||
              !Prev->getOperand(0).isDef() ||
              Prev->getOperand(0).getReg() != Dst)
            continue;

          // The preceding instruction's ST def is no longer dead --
          // the consumer(s) of ST now read it directly from Prev.
          if (int STIdx = Prev->findRegisterDefOperandIdx(TMS9900::ST,
                                                          /*isDead=*/true);
              STIdx != -1) {
            Prev->getOperand(STIdx).setIsDead(false);
          }

          MI.eraseFromParent();
          Changed = true;
          continue;
        }

        // CI Rx,0 optimizations (two tiers):
        //
        // Tier 1 -- full elimination: delete CI Rx,0 when the immediately
        // preceding instruction already set flags for Rx.
        // TMS9900 ALU instructions (MOV, A, S, SOC, SZC, XOR, INC, DEC,
        // SLA, SRA, SRL, ANDI, ORI, INV, NEG, ABS, CLR) all set status
        // bits EQ, LGT, AGT based on the result value -- the same way CI
        // Rx,0 does. So if the immediately preceding instruction already
        // computed the value into TestReg AND set ST, the CI is redundant.
        //
        // Safety constraints for full elimination:
        // 1. Only the immediately preceding instruction (no backward walk)
        // 2. Operand 0 must be the def of TestReg (ensures flags reflect
        //    TestReg, not a secondary def like a pointer in auto-increment)
        // 3. The preceding instruction must set ST
        // 4. The next instruction must be a conditional branch that only
        //    tests EQ/LGT/AGT flags (all TMS9900 conditional jumps do)
        //
        // Tier 2 -- strength reduction: replace CI Rx,0 with MOV Rx,Rx.
        // Both set EQ/LGT/AGT identically for a zero test and both take
        // 14 cycles, but MOV Rx,Rx is 2 bytes vs CI Rx,0 at 4 bytes.
        // This fires when CI Rx,0 cannot be fully eliminated.
        if (Opc == TMS9900::CI) {
          if (!MI.getOperand(1).isImm() ||
              static_cast<int16_t>(MI.getOperand(1).getImm()) != 0)
            continue;

          Register TestReg = MI.getOperand(0).getReg();

          // Try Tier 1: full elimination.
          bool Eliminated = false;
          do {
            // Get the immediately preceding non-debug instruction.
            MachineInstr *Prev = MI.getPrevNode();
            while (Prev && Prev->isDebugInstr())
              Prev = Prev->getPrevNode();
            if (!Prev)
              break;

            // The preceding instruction must:
            // - Define TestReg as its primary result (operand 0, isDef)
            // - Set ST (which all TMS9900 ALU instructions do)
            // - Not be a call, branch, or other non-ALU instruction
            if (Prev->isCall() || Prev->isBranch() || Prev->isReturn())
              break;
            if (!Prev->modifiesRegister(TMS9900::ST, TRI))
              break;

            // Check that operand 0 is a def of TestReg. This ensures the
            // flags reflect TestReg's value (not a secondary def like a
            // pointer update in auto-increment instructions).
            if (Prev->getNumOperands() == 0 ||
                !Prev->getOperand(0).isReg() ||
                !Prev->getOperand(0).isDef() ||
                Prev->getOperand(0).getReg() != TestReg)
              break;

            // Verify the next instruction is a conditional branch.
            // All TMS9900 conditional branches (JEQ, JNE, JGT, JLT, JH,
            // JHE, JL, JLE) test only EQ/LGT/AGT flags, which CI Rx,0
            // and ALU instructions set identically.
            MachineInstr *Next = MI.getNextNode();
            while (Next && Next->isDebugInstr())
              Next = Next->getNextNode();
            if (!Next)
              break;
            unsigned NextOpc = Next->getOpcode();
            if (NextOpc != TMS9900::JEQ && NextOpc != TMS9900::JNE &&
                NextOpc != TMS9900::JGT && NextOpc != TMS9900::JLT &&
                NextOpc != TMS9900::JH && NextOpc != TMS9900::JHE &&
                NextOpc != TMS9900::JL && NextOpc != TMS9900::JLE)
              break;

            // Update ST liveness: the preceding instruction's ST def is
            // no longer dead since the branch now reads it directly.
            if (int STIdx = Prev->findRegisterDefOperandIdx(TMS9900::ST,
                                                            /*isDead=*/true);
                STIdx != -1) {
              Prev->getOperand(STIdx).setIsDead(false);
            }

            MI.eraseFromParent();
            Changed = true;
            Eliminated = true;
          } while (false);

          if (Eliminated)
            continue;

          // Tier 2: CI Rx,0 -> MOV Rx,Rx (2 bytes smaller, same cycles).
          // MOV Rx,Rx sets EQ/LGT/AGT identically to CI Rx,0 for a
          // zero test; it also writes Rx back to itself (no-op on value).
          {
            bool KillUse = MI.getOperand(0).isKill();
            DebugLoc DL = MI.getDebugLoc();

            MachineInstrBuilder MIB =
                BuildMI(MBB, MI, DL, TII->get(TMS9900::MOVrr));
            MIB.addReg(TestReg, RegState::Define);
            MIB.addReg(TestReg, KillUse ? RegState::Kill : 0u);

            // MOVrr implicitly defs ST.  If the original CI had ST as
            // dead, propagate that to the replacement.
            if (MI.registerDefIsDead(TMS9900::ST, TRI)) {
              if (int STIdx =
                      MIB->findRegisterDefOperandIdx(TMS9900::ST,
                                                     /*isDead=*/false,
                                                     /*Overlap=*/true);
                  STIdx != -1)
                MIB->getOperand(STIdx).setIsDead();
            }

            MI.eraseFromParent();
            Changed = true;
            continue;
          }
        }

        // Crr elimination: delete Crr Rx,Ry when one operand is provably
        // zero and the preceding instruction already set flags for the other.
        //
        // Pattern:   <ALU> TestReg, ...    ; defines TestReg, sets ST
        //            Crr ZeroReg, TestReg  ; (or Crr TestReg, ZeroReg)
        //            Jcc label
        //
        // TMS9900 C instruction encoding: Crr rs1(D), rs2(S).
        // Hardware sets: EQ if S==D, LGT if S>D, AGT if S>D.
        //
        // Case A -- zero in rs1 (D field):
        //   EQ if rs2==0, LGT if rs2>0, AGT if rs2>0 (signed).
        //   These match what any ALU instruction sets for rs2.
        //   Safe for all branch types.
        //
        // Case B -- zero in rs2 (S field):
        //   EQ if rs1==0 (matches), LGT if 0>rs1 (never, doesn't match),
        //   AGT if 0>rs1 (signed, doesn't match).
        //   Only EQ flag matches -> safe for JEQ/JNE only.
        if (Opc == TMS9900::Crr) {
          Register Rs1 = MI.getOperand(0).getReg(); // D field
          Register Rs2 = MI.getOperand(1).getReg(); // S field

          // Get the immediately preceding non-debug instruction.
          MachineInstr *Prev = MI.getPrevNode();
          while (Prev && Prev->isDebugInstr())
            Prev = Prev->getPrevNode();
          if (!Prev)
            continue;

          // Prev must not be a call, branch, or return, must set ST,
          // and must define one of {Rs1, Rs2} as its primary result
          // (operand 0 isDef).
          if (Prev->isCall() || Prev->isBranch() || Prev->isReturn())
            continue;
          if (!Prev->modifiesRegister(TMS9900::ST, TRI))
            continue;
          if (Prev->getNumOperands() == 0 ||
              !Prev->getOperand(0).isReg() ||
              !Prev->getOperand(0).isDef())
            continue;

          Register PrevDefReg = Prev->getOperand(0).getReg();
          Register TestReg;   // The register whose flags Prev set.
          Register ZeroReg;   // The register that must be zero.
          bool ZeroInRs1;     // True when ZeroReg is rs1 (D field).

          if (PrevDefReg == Rs2 && PrevDefReg != Rs1) {
            TestReg = Rs2;
            ZeroReg = Rs1;
            ZeroInRs1 = true;  // Case A: all flags safe.
          } else if (PrevDefReg == Rs1 && PrevDefReg != Rs2) {
            TestReg = Rs1;
            ZeroReg = Rs2;
            ZeroInRs1 = false; // Case B: only EQ safe.
          } else {
            continue; // Prev doesn't define either Crr operand,
                       // or both operands are the same register.
          }

          // Verify ZeroReg is provably zero: walk backward (in this BB)
          // to find the most recent def of ZeroReg.  It must be CLR or
          // LI 0, with no intervening instruction that modifies ZeroReg.
          bool ZeroProven = false;
          for (MachineInstr *Scan = Prev->getPrevNode(); Scan;
               Scan = Scan->getPrevNode()) {
            if (Scan->isDebugInstr())
              continue;
            // If this instruction defines ZeroReg, check if it sets it
            // to zero.
            if (Scan->modifiesRegister(ZeroReg, TRI)) {
              unsigned ScanOpc = Scan->getOpcode();
              if (ScanOpc == TMS9900::CLRr &&
                  Scan->getOperand(0).getReg() == ZeroReg) {
                ZeroProven = true;
              } else if (ScanOpc == TMS9900::LI &&
                         Scan->getOperand(0).getReg() == ZeroReg &&
                         Scan->getOperand(1).isImm() &&
                         static_cast<int16_t>(
                             Scan->getOperand(1).getImm()) == 0) {
                ZeroProven = true;
              }
              break; // Stop at the first def of ZeroReg.
            }
            // If this instruction is a call, it may clobber ZeroReg.
            if (Scan->isCall())
              break;
          }

          if (!ZeroProven)
            continue;

          // Verify the next instruction is a conditional branch.
          MachineInstr *Next = MI.getNextNode();
          while (Next && Next->isDebugInstr())
            Next = Next->getNextNode();
          if (!Next)
            continue;
          unsigned NextOpc = Next->getOpcode();

          if (ZeroInRs1) {
            // Case A: all flags match. Any conditional branch is safe.
            if (NextOpc != TMS9900::JEQ && NextOpc != TMS9900::JNE &&
                NextOpc != TMS9900::JGT && NextOpc != TMS9900::JLT &&
                NextOpc != TMS9900::JH && NextOpc != TMS9900::JHE &&
                NextOpc != TMS9900::JL && NextOpc != TMS9900::JLE)
              continue;
          } else {
            // Case B: only EQ flag matches. Only JEQ/JNE are safe.
            if (NextOpc != TMS9900::JEQ && NextOpc != TMS9900::JNE)
              continue;
          }

          // Safe to delete the Crr. Update ST liveness on Prev.
          if (int STIdx = Prev->findRegisterDefOperandIdx(TMS9900::ST,
                                                          /*isDead=*/true);
              STIdx != -1) {
            Prev->getOperand(STIdx).setIsDead(false);
          }

          MI.eraseFromParent();
          Changed = true;
          continue;
        }

        // SRL Rx,8 + ANDI Rx,0x00FF -> SWPB Rx + ANDI Rx,0x00FF
        // SLA Rx,8 + ANDI Rx,0xFF00 -> SWPB Rx + ANDI Rx,0xFF00
        //
        // SWPB exchanges high and low bytes (10 cycles, 2 bytes) vs
        // SRL/SLA by 8 (36 cycles, 4 bytes).  The subsequent ANDI
        // masks off the unwanted byte, producing the same result.
        //   SRL Rx,8:  0xABCD -> 0x00AB
        //   SWPB Rx:   0xABCD -> 0xCDAB, then ANDI 0x00FF -> 0x00AB
        //   SLA Rx,8:  0xABCD -> 0xCD00
        //   SWPB Rx:   0xABCD -> 0xCDAB, then ANDI 0xFF00 -> 0xCD00
        // Savings: 2 bytes, 26 cycles per instance.
        if (Opc == TMS9900::SRLri || Opc == TMS9900::SLAri) {
          if (!MI.getOperand(2).isImm() || MI.getOperand(2).getImm() != 8)
            continue;

          // Find the next non-debug instruction.
          MachineInstr *Next = MI.getNextNode();
          while (Next && Next->isDebugInstr())
            Next = Next->getNextNode();
          if (!Next)
            continue;

          // Next must be ANDI on the same register with the right mask.
          if (Next->getOpcode() != TMS9900::ANDI)
            continue;
          if (!Next->getOperand(2).isImm())
            continue;

          Register Rx = MI.getOperand(0).getReg();
          if (Next->getOperand(0).getReg() != Rx)
            continue;

          uint16_t Mask =
              static_cast<uint16_t>(Next->getOperand(2).getImm());
          bool WantLow = (Opc == TMS9900::SRLri && Mask == 0x00FF);
          bool WantHigh = (Opc == TMS9900::SLAri && Mask == 0xFF00);
          if (!WantLow && !WantHigh)
            continue;

          // Replace the shift with SWPB, keep the ANDI.
          bool DeadDef = MI.getOperand(0).isDead();
          bool KillUse = MI.getOperand(1).isKill();
          DebugLoc DL = MI.getDebugLoc();

          MachineInstrBuilder MIB =
              BuildMI(MBB, MI, DL, TII->get(TMS9900::SWPBr));
          MIB.addReg(Rx, RegState::Define | (DeadDef ? RegState::Dead : 0));
          MIB.addReg(Rx, KillUse ? RegState::Kill : 0);

          // SWPBr implicitly defs ST.  The shift's ST def is dead
          // (the ANDI will set ST next), so mark it dead on the SWPB.
          if (int STIdx =
                  MIB->findRegisterDefOperandIdx(TMS9900::ST, true);
              STIdx != -1 &&
              MI.registerDefIsDead(TMS9900::ST, TRI)) {
            MIB->getOperand(STIdx).setIsDead();
          }

          MI.eraseFromParent();
          Changed = true;
          continue;
        }

        // ANDI Rx,0xFF00 -> delete when the next use of Rx is a MOVB
        // that reads Rx as its source operand, and Rx is killed there.
        //
        // Pattern: MOVB src,Rx  (loads byte into high byte of Rx)
        //          ANDI Rx,0xFF00  (zeros low byte -- 4 bytes, 24 cycles)
        //          MOVB Rx,dst  (stores high byte of Rx)
        //
        // Since MOVB only sends the high byte, and ANDI 0xFF00 only
        // affects the low byte, the ANDI is redundant when the next
        // consumer only cares about the high byte (MOVB source) and
        // Rx is killed (no later reader sees the low byte).
        if (Opc == TMS9900::ANDI) {
          if (!MI.getOperand(2).isImm())
            continue;
          uint16_t Mask =
              static_cast<uint16_t>(MI.getOperand(2).getImm());
          if (Mask != 0xFF00)
            continue;

          Register Rx = MI.getOperand(0).getReg();

          // Find the next non-debug instruction.
          MachineInstr *Next = MI.getNextNode();
          while (Next && Next->isDebugInstr())
            Next = Next->getNextNode();
          if (!Next)
            continue;

          // The next instruction must be a MOVB store/move that reads
          // Rx as its source (high byte) and kills Rx.
          unsigned NextOpc = Next->getOpcode();
          int SrcOpIdx = -1;
          switch (NextOpc) {
          default:
            continue;
          case TMS9900::MOVBmi:  // MOVB Rs,*Rd -- src is operand 1
          case TMS9900::MOVBma:  // MOVB Rs,@addr -- src is operand 1
          case TMS9900::MOVBrr:  // MOVB Rs,Rd -- src is operand 1
            SrcOpIdx = 1;
            break;
          case TMS9900::MOVBmx:  // MOVB Rs,@off(Ri) -- src is operand 2
            SrcOpIdx = 2;
            break;
          }

          if (SrcOpIdx < 0 ||
              SrcOpIdx >= (int)Next->getNumOperands() ||
              !Next->getOperand(SrcOpIdx).isReg() ||
              Next->getOperand(SrcOpIdx).getReg() != Rx)
            continue;

          // Rx must be killed by the MOVB (no later reader of the low
          // byte) -- or at least no other operand of Next reads Rx.
          if (!Next->getOperand(SrcOpIdx).isKill())
            continue;

          // Safe to delete.  Propagate liveness: the ANDI's source
          // operand (operand 1, the tied input) carries the kill flag
          // for Rx from before ANDI.  Transfer that to the MOVB source.
          bool WasKill = MI.getOperand(1).isKill();
          Next->getOperand(SrcOpIdx).setIsKill(WasKill);

          MI.eraseFromParent();
          Changed = true;
          continue;
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
