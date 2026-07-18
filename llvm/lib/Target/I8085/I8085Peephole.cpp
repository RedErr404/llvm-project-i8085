//===-- I8085Peephole.cpp - I8085 peephole optimizations -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains simple I8085 peephole optimizations.
//
//===----------------------------------------------------------------------===//

#include "I8085.h"
#include "I8085InstrInfo.h"
#include "I8085Subtarget.h"
#include "MCTargetDesc/I8085MCTargetDesc.h"

#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "i8085-peephole"

namespace {

// Map a conditional branch opcode to the matching conditional-return opcode.
// Returns 0 if the opcode is not a conditional branch we can turn into a return.
static unsigned condReturnOpc(unsigned BrOpc) {
  switch (BrOpc) {
  case I8085::JZ:  return I8085::RZ;
  case I8085::JNZ: return I8085::RNZ;
  case I8085::JC:  return I8085::RC;
  case I8085::JNC: return I8085::RNC;
  case I8085::JP:  return I8085::RP;
  case I8085::JM:  return I8085::RM;
  case I8085::JPE: return I8085::RPE;
  case I8085::JPO: return I8085::RPO;
  default:         return 0;
  }
}

// True if MBB's only non-debug instruction is a bare, unconditional RET. Such a
// block does no stack cleanup, so a branch to it can be replaced by a
// conditional return without skipping any epilogue work.
static bool isBareReturnBlock(const MachineBasicBlock &MBB) {
  const MachineInstr *Only = nullptr;
  for (const MachineInstr &MI : MBB) {
    if (MI.isDebugInstr())
      continue;
    if (Only)
      return false;
    Only = &MI;
  }
  return Only && Only->getOpcode() == I8085::RET;
}

class I8085Peephole : public MachineFunctionPass {
public:
  static char ID;

  I8085Peephole() : MachineFunctionPass(ID) {
    initializeI8085PeepholePass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override { return "I8085 peephole optimizations"; }

  bool runOnMachineFunction(MachineFunction &MF) override {
    bool Changed = false;
    MachineRegisterInfo &MRI = MF.getRegInfo();
    const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

    for (MachineBasicBlock &MBB : MF) {
      for (auto MI = MBB.begin(); MI != MBB.end();) {
        auto Next = std::next(MI);

        // Conditional-return: a conditional branch whose target block is a lone
        // bare RET becomes the matching conditional return (Jcc L / L: RET ->
        // Rcc). Rcc affects no registers and no flags, and falls through when
        // not taken exactly like the branch did, so this is a pure control-flow
        // substitution that saves 2 bytes (3->1) and a taken jump.
        if (unsigned Rcc = condReturnOpc(MI->getOpcode())) {
          if (MI->getNumOperands() >= 1 && MI->getOperand(0).isMBB()) {
            MachineBasicBlock *TBB = MI->getOperand(0).getMBB();
            if (isBareReturnBlock(*TBB)) {
              const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
              BuildMI(MBB, MI, MI->getDebugLoc(), TII->get(Rcc));
              MI = MBB.erase(MI);

              // The branch edge to the return block is gone. Drop TBB as a CFG
              // successor unless it is still reachable: some other terminator
              // still targets it, or it is the fall-through block.
              bool StillTargeted = false;
              for (const MachineInstr &Term : MBB.terminators())
                for (const MachineOperand &Op : Term.operands())
                  if (Op.isMBB() && Op.getMBB() == TBB)
                    StillTargeted = true;
              auto NextMBB = std::next(MBB.getIterator());
              const MachineBasicBlock *LayoutNext =
                  NextMBB != MF.end() ? &*NextMBB : nullptr;
              bool FallsThrough = MBB.empty() || !MBB.back().isBarrier();
              bool FallsToTBB = FallsThrough && TBB == LayoutNext;
              if (!StillTargeted && !FallsToTBB && MBB.isSuccessor(TBB))
                MBB.removeSuccessor(TBB);

              Changed = true;
              continue;
            }
          }
        }

        // DCR/INR counter idiom. The frontend lowers `--r` / `++r` at the head
        // of a counter loop as
        //   MOV A,r ; SUI 1 ; MOV r,A ; [MOV A,r] ; [ORA A | CPI 0] ; (JZ | JNZ)
        //   MOV A,r ; ADI 1 ; MOV r,A ; [MOV A,r] ; [ORA A | CPI 0] ; (JZ | JNZ)
        // The bracketed instructions are an optional redundant reload of the
        // just-updated value and an optional explicit re-test; either, both, or
        // neither may be present. In every shape the branch's Z comes from the
        // SUI/ADI on r-1 / r+1, which DCR/INR reproduce exactly (Z,S,P,AC), so
        // the whole run collapses to `DCR r` / `INR r` before the branch.
        //
        // DCR/INR differ from the SUI/ADI+test in two ways: they PRESERVE CY
        // (the arithmetic + ORA/CPI cleared it) and they leave the result only
        // in r, not A. So the rewrite is valid only when the branch reads Z
        // (JZ/JNZ, never a CY branch) and both A and the flags are dead after
        // the branch. Liveness comes from the post-RA live-in lists, whose
        // accuracy the machine verifier enforces (a cross-block flag/reg use
        // without a live-in errors out), so a stale-too-small list cannot
        // silently turn this into a miscompile.
        if (MI->getOpcode() == I8085::MOV && MI->getNumOperands() >= 2 &&
            MI->getOperand(0).isReg() &&
            MI->getOperand(0).getReg() == I8085::A &&
            MI->getOperand(1).isReg() &&
            MI->getOperand(1).getReg() != I8085::A &&
            MI->getOperand(1).getReg() != I8085::M) {
          Register R = MI->getOperand(1).getReg();
          auto IsMovAR = [&](MachineBasicBlock::iterator It) {
            return It != MBB.end() && It->getOpcode() == I8085::MOV &&
                   It->getNumOperands() >= 2 && It->getOperand(0).isReg() &&
                   It->getOperand(0).getReg() == I8085::A &&
                   It->getOperand(1).isReg() && It->getOperand(1).getReg() == R;
          };
          auto I1 = std::next(MI);
          bool IsDec = I1 != MBB.end() && I1->getOpcode() == I8085::SUI;
          bool IsInc = I1 != MBB.end() && I1->getOpcode() == I8085::ADI;
          if ((IsDec || IsInc) && I1->getNumOperands() >= 1 &&
              I1->getOperand(0).isImm() && I1->getOperand(0).getImm() == 1) {
            auto I2 = std::next(I1);
            bool MovBack = I2 != MBB.end() && I2->getOpcode() == I8085::MOV &&
                           I2->getNumOperands() >= 2 &&
                           I2->getOperand(0).isReg() &&
                           I2->getOperand(0).getReg() == R &&
                           I2->getOperand(1).isReg() &&
                           I2->getOperand(1).getReg() == I8085::A;
            if (MovBack) {
              // Skip an optional reload MOV A,r, then an optional Z re-test.
              auto Cur = std::next(I2);
              if (IsMovAR(Cur))
                Cur = std::next(Cur);
              if (Cur != MBB.end() &&
                  ((Cur->getOpcode() == I8085::ORA &&
                    Cur->getNumOperands() >= 1 && Cur->getOperand(0).isReg() &&
                    Cur->getOperand(0).getReg() == I8085::A) ||
                   (Cur->getOpcode() == I8085::CPI &&
                    Cur->getNumOperands() >= 1 && Cur->getOperand(0).isImm() &&
                    Cur->getOperand(0).getImm() == 0)))
                Cur = std::next(Cur);
              bool ZBranch = Cur != MBB.end() &&
                             (Cur->getOpcode() == I8085::JZ ||
                              Cur->getOpcode() == I8085::JNZ);
              if (ZBranch) {
                const TargetRegisterInfo *TRI =
                    MF.getSubtarget().getRegisterInfo();
                LivePhysRegs LiveOut(*TRI);
                LiveOut.addLiveOuts(MBB);
                if (!LiveOut.contains(I8085::A) &&
                    !LiveOut.contains(I8085::SREG)) {
                  const TargetInstrInfo *TII =
                      MF.getSubtarget().getInstrInfo();
                  BuildMI(MBB, MI, MI->getDebugLoc(),
                          TII->get(IsDec ? I8085::DCR : I8085::INR))
                      .addReg(R, RegState::Define)
                      .addReg(R);
                  // Erase [MI, branch): the MOV/arith/MOV and any reload/test.
                  MI = MBB.erase(MI, Cur);
                  Changed = true;
                  continue;
                }
              }
            }
          }
        }

        if (MI->getOpcode() == I8085::MOV && MI->getNumOperands() >= 2 &&
            MI->getOperand(0).isReg() && MI->getOperand(1).isReg()) {
          Register Dst = MI->getOperand(0).getReg();
          Register Src = MI->getOperand(1).getReg();
          if (Dst == Src) {
            MI = MBB.erase(MI);
            Changed = true;
            continue;
          }
        }

        if (Next != MBB.end() && MI->getOpcode() == I8085::MOV &&
            Next->getOpcode() == I8085::MOV) {
          if (MI->getNumOperands() >= 2 && Next->getNumOperands() >= 2 &&
              MI->getOperand(0).isReg() && MI->getOperand(1).isReg() &&
              Next->getOperand(0).isReg() && Next->getOperand(1).isReg()) {
            Register Tmp = MI->getOperand(0).getReg();
            Register Src = MI->getOperand(1).getReg();
            Register Dst = Next->getOperand(0).getReg();
            Register Use = Next->getOperand(1).getReg();

            // Reverse-pair elimination: MOV X,Y ; MOV Y,X -> MOV X,Y
            // After "MOV X,Y", register X already holds Y's value and Y is
            // unchanged, so "MOV Y,X" is redundant.
            if (Use == Tmp && Dst == Src &&
                Tmp != I8085::M && Src != I8085::M) {
              bool CanElim = true;
              if (Tmp.isVirtual()) {
                if (!MRI.hasOneUse(Tmp))
                  CanElim = false;
              }
              if (CanElim) {
                auto NextAfter = std::next(Next);
                Next->eraseFromParent();
                Changed = true;
                MI = NextAfter;
                continue;
              }
            }

            bool CanFold = (Use == Tmp) && (Dst != Tmp);
            if (CanFold) {
              if (Tmp.isVirtual()) {
                if (!MRI.hasOneUse(Tmp))
                  CanFold = false;
              } else if (!Next->getOperand(1).isKill()) {
                CanFold = false;
              }
            }

            if (CanFold && Src == I8085::M && Dst == I8085::M)
              CanFold = false;

            if (CanFold) {
              if (Dst == Src) {
                auto NextAfter = std::next(Next);
                Next->eraseFromParent();
                MI->eraseFromParent();
                Changed = true;
                MI = NextAfter;
                continue;
              }

              Next->getOperand(1).setReg(Src);
              Next->getOperand(1).setIsKill(false);
              MI->eraseFromParent();
              Changed = true;
              MI = Next;
              continue;
            }
          }
        }

        if (Next == MBB.end()) {
          ++MI;
          continue;
        }

        if ((MI->getOpcode() == I8085::INX && Next->getOpcode() == I8085::DCX) ||
            (MI->getOpcode() == I8085::DCX && Next->getOpcode() == I8085::INX)) {
          if (MI->getNumOperands() >= 1 && Next->getNumOperands() >= 1 &&
              MI->getOperand(0).isReg() && Next->getOperand(0).isReg()) {
            if (MI->getOperand(0).getReg() == Next->getOperand(0).getReg()) {
              auto NextAfter = std::next(Next);
              Next->eraseFromParent();
              MI->eraseFromParent();
              Changed = true;
              MI = NextAfter;
              continue;
            }
          }
        }

        if (MI->getOpcode() == I8085::XCHG && Next->getOpcode() == I8085::XCHG) {
          auto NextAfter = std::next(Next);
          Next->eraseFromParent();
          MI->eraseFromParent();
          Changed = true;
          MI = NextAfter;
          continue;
        }

        // ORI 0 -> ORA A (1 byte shorter, equivalent flag-setting).
        if (MI->getOpcode() == I8085::ORI && MI->getNumOperands() >= 1 &&
            MI->getOperand(0).isImm() && MI->getOperand(0).getImm() == 0) {
          const MCInstrDesc &Desc = MF.getSubtarget().getInstrInfo()->get(I8085::ORA);
          MI->setDesc(Desc);
          MI->removeOperand(0);
          MI->addOperand(MachineOperand::CreateReg(I8085::A, false));
          Changed = true;
          MI = Next;
          continue;
        }

        // CPI 0 -> ORA A (2 bytes/7 states -> 1 byte/4 states). Both set
        // Z,S,P from A and clear CY and AC, and A|A leaves the accumulator
        // unchanged, so the substitution preserves every architectural
        // effect. CPI defines only SREG whereas ORA also (harmlessly)
        // redefines A with the same value, so build a fresh ORA to get the
        // correct implicit operand list rather than mutating the CPI in place.
        if (MI->getOpcode() == I8085::CPI && MI->getNumOperands() >= 1 &&
            MI->getOperand(0).isImm() && MI->getOperand(0).getImm() == 0) {
          const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
          BuildMI(MBB, MI, MI->getDebugLoc(), TII->get(I8085::ORA))
              .addReg(I8085::A);
          MI = MBB.erase(MI);
          Changed = true;
          continue;
        }

        // Redundant flag-test elimination: ANI/ORI/XRI/ANA/ORA/XRA (and
        // their memory-indirect variants) all set Z, S, P flags and clear CY
        // exactly like ORA A would, so ORA A immediately after is redundant.
        if (MI->getOpcode() == I8085::ORA && MI->getNumOperands() >= 1 &&
            MI->getOperand(0).isReg() && MI->getOperand(0).getReg() == I8085::A &&
            MI != MBB.begin()) {
          auto Prev = std::prev(MI);
          unsigned PrevOpc = Prev->getOpcode();
          if (PrevOpc == I8085::ANI || PrevOpc == I8085::ORI ||
              PrevOpc == I8085::XRI || PrevOpc == I8085::ANA ||
              PrevOpc == I8085::ORA || PrevOpc == I8085::XRA ||
              PrevOpc == I8085::ANA_M || PrevOpc == I8085::ORA_M ||
              PrevOpc == I8085::XRA_M) {
            MI = MBB.erase(MI);
            Changed = true;
            continue;
          }
        }

        if (MI->getOpcode() != I8085::LXI || Next->getNumOperands() == 0) {
          ++MI;
          continue;
        }

        unsigned NextOpc = Next->getOpcode();
        if (NextOpc != I8085::INX && NextOpc != I8085::DCX) {
          ++MI;
          continue;
        }

        if (!MI->getOperand(0).isReg() || !MI->getOperand(1).isImm() ||
            !Next->getOperand(0).isReg()) {
          ++MI;
          continue;
        }

        Register LXIReg = MI->getOperand(0).getReg();
        Register StepReg = Next->getOperand(0).getReg();
        if (LXIReg != StepReg) {
          ++MI;
          continue;
        }

        int64_t Imm = MI->getOperand(1).getImm();
        uint16_t Val = static_cast<uint16_t>(Imm);
        if (NextOpc == I8085::INX)
          Val = static_cast<uint16_t>(Val + 1);
        else
          Val = static_cast<uint16_t>(Val - 1);

        MI->getOperand(1).setImm(Val);
        Next->eraseFromParent();
        Changed = true;
        ++MI;
        continue;
      }

      Changed |= eliminateRedundantMoves(MBB, TRI);
    }

    return Changed;
  }

  // Post-expansion redundant-copy elimination. Pseudo expansion (LOAD_16 and
  // friends) leaves behind MOV round-trips such as
  //   MOV B,H ; MOV C,L ; MOV H,B ; MOV L,C
  // (a register pair copied out to another pair and straight back). The normal
  // MachineCopyPropagation runs before this target's pseudo expansion and never
  // sees them. Forward-scan each block giving the seven 8-bit leaf registers a
  // symbolic value id; a `MOV dst,src` whose dst already holds src's value is a
  // no-op and is erased (MOV touches no flags, so this is always safe). Any
  // other def - a 16-bit def like LXI H / INX H / DAD, an ALU def of A, or a
  // load `MOV r,M` - invalidates every overlapping leaf.
  bool eliminateRedundantMoves(MachineBasicBlock &MBB,
                               const TargetRegisterInfo *TRI) {
    static const unsigned Leaves[7] = {I8085::A, I8085::B, I8085::C, I8085::D,
                                       I8085::E, I8085::H, I8085::L};
    unsigned Val[7];
    unsigned NextId = 1;
    for (int i = 0; i < 7; ++i)
      Val[i] = NextId++;
    auto leafIdx = [&](unsigned R) -> int {
      for (int i = 0; i < 7; ++i)
        if (Leaves[i] == R)
          return i;
      return -1;
    };
    auto invalidate = [&](unsigned R) {
      for (int i = 0; i < 7; ++i)
        if (TRI->regsOverlap(R, Leaves[i]))
          Val[i] = NextId++;
    };

    bool Changed = false;
    for (auto It = MBB.begin(); It != MBB.end();) {
      MachineInstr &MI = *It;
      auto Nxt = std::next(It);

      if (MI.getOpcode() == I8085::MOV && MI.getOperand(0).isReg() &&
          MI.getOperand(1).isReg()) {
        int di = leafIdx(MI.getOperand(0).getReg());
        int si = leafIdx(MI.getOperand(1).getReg());
        if (di >= 0 && si >= 0) {
          // Pure leaf-to-leaf copy (neither operand is the memory pseudo M).
          if (Val[di] == Val[si]) {
            It = MBB.erase(It); // dst already holds src's value: redundant.
            Changed = true;
            continue;
          }
          Val[di] = Val[si];
          It = Nxt;
          continue;
        }
        // A MOV touching M (a load / store): a leaf dst gets an unknown value.
        if (di >= 0)
          Val[di] = NextId++;
        It = Nxt;
        continue;
      }

      // Any other instruction: invalidate the leaves it (implicitly) defines.
      for (const MachineOperand &MO : MI.operands())
        if (MO.isReg() && MO.isDef() && MO.getReg())
          invalidate(MO.getReg());
      for (MCPhysReg R : MI.getDesc().implicit_defs())
        invalidate(R);

      It = Nxt;
    }
    return Changed;
  }
};
} // end anonymous namespace

char I8085Peephole::ID = 0;

INITIALIZE_PASS(I8085Peephole, "i8085-peephole",
                "I8085 peephole optimizations", false, false)

FunctionPass *llvm::createI8085PeepholePass() { return new I8085Peephole(); }
