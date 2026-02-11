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
//  - ANDI Rx,0xFF00 -> delete when followed by AI Rx,N*256 then MOVB source
//  - SRL Rx,8 + ANDI Rx,0x00FF -> SWPB Rx + ANDI Rx,0x00FF
//  - SLA Rx,8 + ANDI Rx,0xFF00 -> SWPB Rx + ANDI Rx,0xFF00
//  - SLA Rx,8 + MOVB Rx,dst   -> SWPB Rx + MOVB Rx,dst
//  - SRL Rx,8 + CI Rx,N -> CI Rx,(N<<8) when N<=0xFF and Rx dead after CI
//  - SRL Rx,8 + SRL Ry,8 + Crr Rx,Ry -> CBrr Rx,Ry (byte compare folding)
//  - Dead first load when consecutive loads define same register
//  - AI 0 removed when status flags are dead
//  - INV Rx + INV Rx (consecutive) -> delete both (double inversion cancels)
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

  // For loads (MOV *Rs+, Rd): if AddrReg == ValueReg, the loaded value
  // overwrites the auto-incremented address, losing the increment.
  // e.g. MOV *R0+, R0 loads from *R0 into R0, clobbering the R0+2 result.
  if (IsLoad && AddrReg == ValueReg)
    return false;

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

/// Return the operand index of the source register (the register whose
/// HIGH byte is read) in a MOVB store/move instruction, or -1 if \p MI
/// is not a MOVB that reads a source register.
static int getMovbSrcOpIdx(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return -1;
  case TMS9900::MOVBmi:  // MOVB Rs,*Rd -- src is operand 1
  case TMS9900::MOVBma:  // MOVB Rs,@addr -- src is operand 1
  case TMS9900::MOVBrr:  // MOVB Rs,Rd -- src is operand 1
    return 1;
  case TMS9900::MOVBmx:  // MOVB Rs,@off(Ri) -- src is operand 2
  case TMS9900::MOVBmpi: // MOVB Rs,*Rd+ -- src is operand 2
    return 2;
  }
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

        // Double INV cancellation: INV Rx followed by INV Rx -> delete both.
        // This pattern arises from the AND pseudo expansion when two
        // consecutive ANDs use the same source: the first AND's restore
        // INV and the second AND's initial INV cancel each other.
        //
        // The first INV's ST def is always dead (immediately overwritten by
        // the second INV). For the second INV, we must verify that ST is
        // dead too (either marked dead, or the next instruction also defs ST).
        if (Opc == TMS9900::INVr) {
          MachineInstr *Next = MI.getNextNode();
          while (Next && Next->isDebugInstr())
            Next = Next->getNextNode();
          if (Next && Next->getOpcode() == TMS9900::INVr &&
              MI.getOperand(0).getReg() == Next->getOperand(0).getReg()) {
            // Check that the second INV's ST def is dead.
            bool SecondSTDead = Next->registerDefIsDead(TMS9900::ST, TRI);
            if (!SecondSTDead) {
              // Check if the instruction after the pair also defines ST,
              // making the second INV's ST def effectively dead.
              MachineInstr *After = Next->getNextNode();
              while (After && After->isDebugInstr())
                After = After->getNextNode();
              if (After && After->modifiesRegister(TMS9900::ST, TRI))
                SecondSTDead = true;
            }
            if (SecondSTDead) {
              // Set the iterator to the instruction after Next before
              // erasing, since It may currently point at Next.
              It = std::next(MachineBasicBlock::instr_iterator(Next));
              Next->eraseFromParent();
              MI.eraseFromParent();
              Changed = true;
              continue;
            }
          }
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

          // CLR and SETO do NOT set status flags (TMS9900 Data Manual p.24),
          // but LI DOES set status flags (Data Manual p.26).  Only convert
          // when the LI's ST def is dead (flags not needed).
          if (!MI.registerDefIsDead(TMS9900::ST, TRI))
            continue;

          Register Reg = MI.getOperand(0).getReg();
          bool DeadDef = MI.getOperand(0).isDead();
          DebugLoc DL = MI.getDebugLoc();
          MachineInstrBuilder MIB =
              BuildMI(MBB, MI, DL, TII->get(NewOpc));
          MIB.addReg(Reg, RegState::Define | (DeadDef ? RegState::Dead : 0));

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

          // XOR sets status flags but CLR does NOT (TMS9900 Data Manual p.24).
          // Only convert when the XOR's ST def is dead (flags not needed).
          if (!MI.registerDefIsDead(TMS9900::ST, TRI))
            continue;

          bool DeadDef = MI.getOperand(0).isDead();
          DebugLoc DL = MI.getDebugLoc();
          MachineInstrBuilder MIB =
              BuildMI(MBB, MI, DL, TII->get(TMS9900::CLRr));
          MIB.addReg(Dst, RegState::Define | (DeadDef ? RegState::Dead : 0));

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
        // SLA, SRA, SRL, ANDI, ORI, INV, NEG, ABS) all set status
        // bits EQ, LGT, AGT based on the result value -- the same way CI
        // NOTE: CLR and SETO do NOT set status flags (TMS9900 Data Manual).
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

        // Crr optimizations:
        //
        // Opt 1 -- Zero-register elimination:
        //   Delete Crr Rx,Ry when one operand is provably zero and the
        //   preceding instruction already set flags for the other.
        //
        // Opt 2 -- CB folding (byte compare):
        //   Replace Crr Rx,Ry with CBrr Rx,Ry when both operands were
        //   produced by SRLri 8 (byte-in-low-position from a MOVB load).
        //   Delete both SRLs, since CBrr compares HIGH bytes directly
        //   and the pre-SRL values have the bytes in the HIGH position.
        if (Opc == TMS9900::Crr) {
          Register Rs1 = MI.getOperand(0).getReg(); // D field
          Register Rs2 = MI.getOperand(1).getReg(); // S field

          // --- Opt 1: Zero-register elimination ---
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
          bool ZeroEliminated = false;
          do {
            // Get the immediately preceding non-debug instruction.
            MachineInstr *Prev = MI.getPrevNode();
            while (Prev && Prev->isDebugInstr())
              Prev = Prev->getPrevNode();
            if (!Prev)
              break;

            // Prev must not be a call, branch, or return, must set ST,
            // and must define one of {Rs1, Rs2} as its primary result
            // (operand 0 isDef).
            if (Prev->isCall() || Prev->isBranch() || Prev->isReturn())
              break;
            if (!Prev->modifiesRegister(TMS9900::ST, TRI))
              break;
            if (Prev->getNumOperands() == 0 ||
                !Prev->getOperand(0).isReg() ||
                !Prev->getOperand(0).isDef())
              break;

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
              break; // Prev doesn't define either Crr operand,
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
              break;

            // Verify the next instruction is a conditional branch.
            MachineInstr *Next = MI.getNextNode();
            while (Next && Next->isDebugInstr())
              Next = Next->getNextNode();
            if (!Next)
              break;
            unsigned NextOpc = Next->getOpcode();

            if (ZeroInRs1) {
              // Case A: all flags match. Any conditional branch is safe.
              if (NextOpc != TMS9900::JEQ && NextOpc != TMS9900::JNE &&
                  NextOpc != TMS9900::JGT && NextOpc != TMS9900::JLT &&
                  NextOpc != TMS9900::JH && NextOpc != TMS9900::JHE &&
                  NextOpc != TMS9900::JL && NextOpc != TMS9900::JLE)
                break;
            } else {
              // Case B: only EQ flag matches. Only JEQ/JNE are safe.
              if (NextOpc != TMS9900::JEQ && NextOpc != TMS9900::JNE)
                break;
            }

            // Safe to delete the Crr. Update ST liveness on Prev.
            if (int STIdx = Prev->findRegisterDefOperandIdx(TMS9900::ST,
                                                            /*isDead=*/true);
                STIdx != -1) {
              Prev->getOperand(STIdx).setIsDead(false);
            }

            MI.eraseFromParent();
            Changed = true;
            ZeroEliminated = true;
          } while (false);

          if (ZeroEliminated)
            continue;

          // --- Opt 2: CB folding (byte compare) ---
          //
          // Pattern:
          //   MOVB *Ra, Rx            ; Rx has byte in HIGH position
          //   MOVB *Rb, Ry            ; Ry has byte in HIGH position
          //   SRLri Rx, 8             ; shift byte to LOW position
          //   SRLri Ry, 8             ; shift byte to LOW position
          //   Crr Rx, Ry              ; compare words (bytes in low position)
          //   Jcc target
          //
          // Becomes:
          //   MOVB *Ra, Rx            ; Rx has byte in HIGH position
          //   MOVB *Rb, Ry            ; Ry has byte in HIGH position
          //   CBrr Rx, Ry             ; compare HIGH bytes directly
          //   Jcc target
          //
          // CB compares the high bytes of both operands and sets EQ/LGT/AGT
          // identically to how C would compare the full words when those
          // words contain zero-extended byte values in the low position.
          //
          // Safety:
          //  - Both Crr operands must be killed (dead after compare), since
          //    the registers will retain their un-shifted values (byte in
          //    high position) instead of the shifted values.
          //  - Both operands must be defined by SRLri X,8 with no
          //    intervening use of those registers between SRL and Crr.
          //  - No intervening instruction between the SRLs and Crr may
          //    read ST (we're deleting SRLs which set ST).
          //
          // Savings: 8 bytes (two SRL instructions), ~56 cycles.
          do {
            // Both operands must be killed at the Crr.
            if (!MI.getOperand(0).isKill() || !MI.getOperand(1).isKill())
              break;

            // Rs1 and Rs2 must be different registers.
            if (Rs1 == Rs2)
              break;

            // Walk backward to find the SRLri instructions that define
            // Rs1 and Rs2.  They must be adjacent or only separated by
            // each other / debug instructions, and between them and
            // the Crr there must be no reads/writes of Rs1 or Rs2.
            MachineInstr *Srl1 = nullptr; // SRLri for Rs1
            MachineInstr *Srl2 = nullptr; // SRLri for Rs2
            bool Unsafe = false;

            for (MachineInstr *Scan = MI.getPrevNode(); Scan;
                 Scan = Scan->getPrevNode()) {
              if (Scan->isDebugInstr())
                continue;

              // Check if this instruction defines Rs1 or Rs2.
              bool DefsRs1 = !Srl1 && Scan->modifiesRegister(Rs1, TRI);
              bool DefsRs2 = !Srl2 && Scan->modifiesRegister(Rs2, TRI);

              if (DefsRs1) {
                // Must be SRLri Rs1, 8.
                if (Scan->getOpcode() != TMS9900::SRLri ||
                    Scan->getOperand(0).getReg() != Rs1 ||
                    !Scan->getOperand(2).isImm() ||
                    Scan->getOperand(2).getImm() != 8) {
                  Unsafe = true;
                  break;
                }
                Srl1 = Scan;
              }

              if (DefsRs2) {
                // Must be SRLri Rs2, 8.
                if (Scan->getOpcode() != TMS9900::SRLri ||
                    Scan->getOperand(0).getReg() != Rs2 ||
                    !Scan->getOperand(2).isImm() ||
                    Scan->getOperand(2).getImm() != 8) {
                  Unsafe = true;
                  break;
                }
                Srl2 = Scan;
              }

              // If we found both SRLs, we're done scanning.
              if (Srl1 && Srl2)
                break;

              // If this instruction reads a register we haven't found
              // the SRL for yet (other than a def we just matched),
              // the pattern is broken: there's a use between SRL and Crr.
              if (!Srl1 && !DefsRs1 && Scan->readsRegister(Rs1, TRI)) {
                Unsafe = true;
                break;
              }
              if (!Srl2 && !DefsRs2 && Scan->readsRegister(Rs2, TRI)) {
                Unsafe = true;
                break;
              }

              // Bail on calls/branches (they may clobber registers or
              // read ST).
              if (Scan->isCall() || Scan->isBranch() || Scan->isReturn()) {
                Unsafe = true;
                break;
              }
            }

            if (Unsafe || !Srl1 || !Srl2)
              break;

            // Verify the SRL ST defs are dead (no consumer between SRL
            // and Crr reads ST).  The SRLs should already have dead ST
            // since the Crr (or a later instruction) sets ST.
            if (!Srl1->registerDefIsDead(TMS9900::ST, TRI) ||
                !Srl2->registerDefIsDead(TMS9900::ST, TRI))
              break;

            // Build CBrr with the same operands as the Crr.
            DebugLoc DL = MI.getDebugLoc();
            MachineInstrBuilder MIB =
                BuildMI(MBB, MI, DL, TII->get(TMS9900::CBrr));
            MIB.addReg(Rs1, RegState::Kill);
            MIB.addReg(Rs2, RegState::Kill);

            // CBrr implicitly defs ST (live, consumed by the branch).
            // The implicit def is added automatically by BuildMI from
            // the instruction descriptor.

            // Delete the Crr and both SRLri instructions.
            // Update the iterator if it points at one of the SRLs.
            MI.eraseFromParent();
            Srl1->eraseFromParent();
            Srl2->eraseFromParent();
            Changed = true;
          } while (false);

          continue;
        }

        // SRL Rx,8 + ANDI Rx,0x00FF -> SWPB Rx + ANDI Rx,0x00FF
        // SLA Rx,8 + ANDI Rx,0xFF00 -> SWPB Rx + ANDI Rx,0xFF00
        // SLA Rx,8 + MOVB Rx,dst   -> SWPB Rx + MOVB Rx,dst
        //
        // SWPB exchanges high and low bytes (10 cycles, 2 bytes) vs
        // SRL/SLA by 8 (36 cycles, 4 bytes).  The subsequent ANDI
        // masks off the unwanted byte, producing the same result.
        //   SRL Rx,8:  0xABCD -> 0x00AB
        //   SWPB Rx:   0xABCD -> 0xCDAB, then ANDI 0x00FF -> 0x00AB
        //   SLA Rx,8:  0xABCD -> 0xCD00
        //   SWPB Rx:   0xABCD -> 0xCDAB, then ANDI 0xFF00 -> 0xCD00
        //
        // For SLA Rx,8 + MOVB: MOVB only reads the HIGH byte of Rx.
        // Both SLA 8 and SWPB put the original low byte into the high
        // byte position. The low byte differs (SLA zeros it, SWPB puts
        // old high byte there) but MOVB ignores it.
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

          Register Rx = MI.getOperand(0).getReg();
          bool CanReplace = false;

          // Case 1: shift + ANDI with matching mask.
          if (Next->getOpcode() == TMS9900::ANDI &&
              Next->getOperand(2).isImm() &&
              Next->getOperand(0).getReg() == Rx) {
            uint16_t Mask =
                static_cast<uint16_t>(Next->getOperand(2).getImm());
            bool WantLow = (Opc == TMS9900::SRLri && Mask == 0x00FF);
            bool WantHigh = (Opc == TMS9900::SLAri && Mask == 0xFF00);
            CanReplace = WantLow || WantHigh;
          }

          // Case 2: SLA Rx,8 + MOVB that reads Rx as source.
          // MOVB only uses the high byte, so SWPB produces the same
          // observable result as SLA 8.
          if (!CanReplace && Opc == TMS9900::SLAri) {
            int SrcIdx = getMovbSrcOpIdx(*Next);
            if (SrcIdx >= 0 &&
                SrcIdx < (int)Next->getNumOperands() &&
                Next->getOperand(SrcIdx).isReg() &&
                Next->getOperand(SrcIdx).getReg() == Rx) {
              CanReplace = true;
            }
          }

          // Case 3: SRL Rx,8 + CI Rx,N -> CI Rx,(N<<8)
          //
          // Byte loads produce values in the HIGH byte of a register.
          // The idiom SRL Rx,8 shifts the byte to the low position,
          // then CI Rx,N compares against a small constant.  Instead,
          // we can skip the SRL and compare the un-shifted value
          // directly: CI Rx,(N<<8).
          //
          // IMPORTANT: SRL Rx,8 zeroes the high byte as a side effect
          // of shifting right.  If the LOW byte of Rx is unknown before
          // the SRL (e.g. after MOVB which only sets the high byte,
          // leaving the low byte unchanged), skipping SRL leaves that
          // garbage in the low byte and the 16-bit CI comparison
          // becomes incorrect.  We must verify that the low byte of Rx
          // is known-zero before the SRL to ensure CI Rx,(N<<8)
          // produces the same comparison result as SRL+CI Rx,N.
          //
          // Safety:
          //  - Only SRL (not SLA) with count == 8.
          //  - N must fit in 8 bits (N <= 0xFF) so N<<8 fits in 16 bits.
          //  - Rx must be dead after the CI instruction, because we are
          //    leaving Rx un-shifted (the byte is still in the high
          //    position instead of the low position).  We check that the
          //    CI operand has isKill(), meaning no later instruction
          //    reads Rx.
          //  - The low byte of Rx must be zero before SRL.  We check
          //    that the immediately preceding def of Rx is a word-width
          //    instruction with a known-zero low byte (e.g. ANDI with
          //    mask & 0xFF == 0, SLA by >= 8, LI with imm & 0xFF == 0,
          //    or CLR).  MOVB does NOT qualify since it only sets the
          //    high byte and leaves the low byte unchanged.
          //  - No instruction between SRL and CI reads ST (SRL sets
          //    flags; we're removing it, so the flags must not be
          //    consumed).  Since they are adjacent, this is satisfied.
          //
          // Saves 2 bytes (SRL is 2 bytes) and ~28 cycles per instance.
          if (!CanReplace && Opc == TMS9900::SRLri &&
              Next->getOpcode() == TMS9900::CI &&
              Next->getOperand(0).getReg() == Rx &&
              Next->getOperand(1).isImm()) {
            int64_t CIImm = Next->getOperand(1).getImm();
            uint16_t N = static_cast<uint16_t>(CIImm);
            if (N <= 0xFF && Next->getOperand(0).isKill()) {
              // Verify the low byte of Rx is zero before the SRL.
              // Walk backward to the immediately preceding non-debug
              // instruction that defines Rx.
              bool LowByteZero = false;
              MachineInstr *Prev = MI.getPrevNode();
              while (Prev && Prev->isDebugInstr())
                Prev = Prev->getPrevNode();
              if (Prev && Prev->modifiesRegister(Rx, TRI)) {
                unsigned PrevOpc = Prev->getOpcode();
                // ANDI Rx, mask: low byte zero if mask & 0xFF == 0.
                if (PrevOpc == TMS9900::ANDI &&
                    Prev->getOperand(0).getReg() == Rx &&
                    Prev->getOperand(2).isImm()) {
                  uint16_t Mask =
                      static_cast<uint16_t>(Prev->getOperand(2).getImm());
                  if ((Mask & 0xFF) == 0)
                    LowByteZero = true;
                }
                // SLA Rx,N where N >= 8: shifts left, zeroing low byte.
                else if (PrevOpc == TMS9900::SLAri &&
                         Prev->getOperand(0).getReg() == Rx &&
                         Prev->getOperand(2).isImm() &&
                         Prev->getOperand(2).getImm() >= 8) {
                  LowByteZero = true;
                }
                // LI Rx, imm: low byte zero if imm & 0xFF == 0.
                else if (PrevOpc == TMS9900::LI &&
                         Prev->getOperand(0).getReg() == Rx &&
                         Prev->getOperand(1).isImm()) {
                  uint16_t Imm =
                      static_cast<uint16_t>(Prev->getOperand(1).getImm());
                  if ((Imm & 0xFF) == 0)
                    LowByteZero = true;
                }
                // CLR Rx: both bytes zero.
                else if (PrevOpc == TMS9900::CLRr &&
                         Prev->getOperand(0).getReg() == Rx) {
                  LowByteZero = true;
                }
              }

              if (LowByteZero) {
                // Replace CI Rx,N with CI Rx,(N<<8) and delete the SRL.
                Next->getOperand(1).setImm(static_cast<int64_t>(N << 8));

                MI.eraseFromParent();
                Changed = true;
                continue;
              }
            }
          }

          if (!CanReplace)
            continue;

          // Replace the shift with SWPB, keep the following instruction.
          bool DeadDef = MI.getOperand(0).isDead();
          bool KillUse = MI.getOperand(1).isKill();
          DebugLoc DL = MI.getDebugLoc();

          MachineInstrBuilder MIB =
              BuildMI(MBB, MI, DL, TII->get(TMS9900::SWPBr));
          MIB.addReg(Rx, RegState::Define | (DeadDef ? RegState::Dead : 0));
          MIB.addReg(Rx, KillUse ? RegState::Kill : 0);

          // SWPB does not affect ST (unlike the SRL/SLA it replaces),
          // so flags set before the shift are preserved across SWPB.

          MI.eraseFromParent();
          Changed = true;
          continue;
        }

        // ANDI Rx,0xFF00 -> delete when the eventual consumer of Rx
        // only reads the HIGH byte (MOVB source) and Rx is killed there.
        //
        // Case 1 (direct): ANDI + MOVB
        //   MOVB src,Rx  (loads byte into high byte of Rx)
        //   ANDI Rx,0xFF00  (zeros low byte -- 4 bytes, 24 cycles)
        //   MOVB Rx,dst  (stores high byte of Rx)
        //
        // Case 2 (through AI): ANDI + AI + MOVB
        //   MOVB src,Rx  (loads byte into high byte of Rx)
        //   ANDI Rx,0xFF00  (zeros low byte)
        //   AI Rx,N*256  (add to high byte; N*256 has 0 in low byte)
        //   MOVB Rx,dst  (stores high byte)
        //
        // In Case 2, since AI adds a multiple of 256, the low byte
        // of Rx does not carry into the high byte (adding 0x00 to the
        // low byte cannot generate a carry), so clearing the low byte
        // with ANDI is unnecessary.
        //
        // Since MOVB only sends the high byte, and ANDI 0xFF00 only
        // affects the low byte, the ANDI is redundant when the
        // consumer only cares about the high byte (MOVB source) and
        // Rx is killed (no later reader sees the low byte).
        //
        // Savings: 4 bytes, 24 cycles per instance.
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

          // Check for optional intervening AI Rx,N where N is a
          // multiple of 256 (low byte is 0x00).  This is safe because
          // adding a value with 0 in the low byte cannot carry from
          // the low byte to the high byte.
          MachineInstr *Consumer = Next;
          bool SkippedAI = false;
          if (Next->getOpcode() == TMS9900::AI &&
              Next->getOperand(0).getReg() == Rx &&
              Next->getOperand(1).getReg() == Rx &&
              Next->getOperand(2).isImm()) {
            int16_t AIImm =
                static_cast<int16_t>(Next->getOperand(2).getImm());
            // Check that the immediate is a multiple of 256 (low byte
            // is 0).  This means the AI only affects the high byte.
            if ((AIImm & 0xFF) == 0) {
              Consumer = Next->getNextNode();
              while (Consumer && Consumer->isDebugInstr())
                Consumer = Consumer->getNextNode();
              if (!Consumer)
                continue;
              SkippedAI = true;
            }
          }

          // Consumer must be a MOVB that reads Rx as source (high byte).
          int SrcOpIdx = getMovbSrcOpIdx(*Consumer);

          if (SrcOpIdx < 0 ||
              SrcOpIdx >= (int)Consumer->getNumOperands() ||
              !Consumer->getOperand(SrcOpIdx).isReg() ||
              Consumer->getOperand(SrcOpIdx).getReg() != Rx)
            continue;

          // Rx must be killed by the MOVB (no later reader of the low
          // byte) -- or at least no other operand of Consumer reads Rx.
          if (!Consumer->getOperand(SrcOpIdx).isKill())
            continue;

          // Safe to delete.  Propagate liveness: the ANDI's source
          // operand (operand 1, the tied input) carries the kill flag
          // for Rx from before ANDI.  Transfer that to the next
          // instruction's use of Rx.
          bool WasKill = MI.getOperand(1).isKill();
          if (SkippedAI) {
            // AI is between ANDI and MOVB.  Transfer kill to AI input.
            Next->getOperand(1).setIsKill(WasKill);
          } else {
            Consumer->getOperand(SrcOpIdx).setIsKill(WasKill);
          }

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
