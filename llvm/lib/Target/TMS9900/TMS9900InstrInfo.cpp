//===-- TMS9900InstrInfo.cpp - TMS9900 Instruction Information ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the TMS9900 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "TMS9900InstrInfo.h"
#include "TMS9900.h"
#include "TMS9900Subtarget.h"
#include "TMS9900TargetMachine.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

#define DEBUG_TYPE "tms9900-instr-info"

#define GET_INSTRINFO_CTOR_DTOR
#include "TMS9900GenInstrInfo.inc"

static bool isCondBranchOpcode(unsigned Opc) {
  switch (Opc) {
  case TMS9900::JEQ:
  case TMS9900::JNE:
  case TMS9900::JGT:
  case TMS9900::JLT:
  case TMS9900::JH:
  case TMS9900::JHE:
  case TMS9900::JL:
  case TMS9900::JLE:
  case TMS9900::JOC:
  case TMS9900::JNC:
  case TMS9900::JNO:
  case TMS9900::JOP:
    return true;
  default:
    return false;
  }
}

static unsigned getOppositeCondBranchOpcode(unsigned Opc) {
  switch (Opc) {
  case TMS9900::JEQ: return TMS9900::JNE;
  case TMS9900::JNE: return TMS9900::JEQ;
  case TMS9900::JL:  return TMS9900::JHE; // unsigned <  -> unsigned >=
  case TMS9900::JHE: return TMS9900::JL;  // unsigned >= -> unsigned <
  case TMS9900::JH:  return TMS9900::JLE; // unsigned >  -> unsigned <=
  case TMS9900::JLE: return TMS9900::JH;  // unsigned <= -> unsigned >
  case TMS9900::JOC: return TMS9900::JNC;
  case TMS9900::JNC: return TMS9900::JOC;
  default:
    return 0;
  }
}

static unsigned getJumpOpcodeForCC(ISD::CondCode CC) {
  switch (CC) {
  default:
    llvm_unreachable("Unknown condition code");
  case ISD::SETEQ:
    return TMS9900::JEQ;
  case ISD::SETNE:
    return TMS9900::JNE;
  case ISD::SETGT:
    return TMS9900::JGT;
  case ISD::SETLT:
    return TMS9900::JLT;
  case ISD::SETGE:
    return TMS9900::JGT;
  case ISD::SETLE:
    return TMS9900::JLT;
  case ISD::SETUGT:
    return TMS9900::JH;
  case ISD::SETUGE:
    return TMS9900::JHE;
  case ISD::SETULT:
    return TMS9900::JL;
  case ISD::SETULE:
    return TMS9900::JLE;
  }
}

TMS9900InstrInfo::TMS9900InstrInfo(const TMS9900Subtarget &STI)
    : TMS9900GenInstrInfo(TMS9900::ADJCALLSTACKDOWN, TMS9900::ADJCALLSTACKUP),
      RI(STI) {}

void TMS9900InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator I,
                                    const DebugLoc &DL, MCRegister DestReg,
                                    MCRegister SrcReg, bool KillSrc) const {
  // Use MOV instruction for register copy
  // MOV Rs,Rd copies Rs to Rd
  BuildMI(MBB, I, DL, get(TMS9900::MOVrr), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
}

void TMS9900InstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool isKill, int FrameIndex, const TargetRegisterClass *RC,
    const TargetRegisterInfo *TRI, Register VReg) const {
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();

  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIndex),
      MachineMemOperand::MOStore, MFI.getObjectSize(FrameIndex),
      MFI.getObjectAlign(FrameIndex));

  // Use MOV_FI_Store: MOV Rs,@offset(Ri) where Ri will be R10 (SP)
  // Operands: (base register, offset, source register)
  // The frame index will be eliminated later by eliminateFrameIndex
  BuildMI(MBB, MI, DL, get(TMS9900::MOV_FI_Store))
      .addFrameIndex(FrameIndex)  // Will become R10
      .addImm(0)                  // Offset (will be resolved by eliminateFrameIndex)
      .addReg(SrcReg, getKillRegState(isKill))
      .addMemOperand(MMO);
}

void TMS9900InstrInfo::loadRegFromStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register DestReg,
    int FrameIndex, const TargetRegisterClass *RC,
    const TargetRegisterInfo *TRI, Register VReg) const {
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();

  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIndex),
      MachineMemOperand::MOLoad, MFI.getObjectSize(FrameIndex),
      MFI.getObjectAlign(FrameIndex));

  // Use MOV_FI_Load: MOV @offset(Ri),Rd where Ri will be R10 (SP)
  // Operands: (destination register, base register, offset)
  // The frame index will be eliminated later by eliminateFrameIndex
  BuildMI(MBB, MI, DL, get(TMS9900::MOV_FI_Load), DestReg)
      .addFrameIndex(FrameIndex)  // Will become R10
      .addImm(0)                  // Offset (will be resolved by eliminateFrameIndex)
      .addMemOperand(MMO);
}

bool TMS9900InstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                      MachineBasicBlock *&TBB,
                                      MachineBasicBlock *&FBB,
                                      SmallVectorImpl<MachineOperand> &Cond,
                                      bool AllowModify) const {
  // Start from the bottom of the block and work up
  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;

    if (I->isDebugInstr())
      continue;

    // If we see a non-terminator, we're done
    if (!isUnpredicatedTerminator(*I))
      break;

    // A terminator that isn't a branch can't be handled.
    if (!I->isBranch())
      return true;

    // Cannot handle indirect branches.
    if (I->isIndirectBranch())
      return true;

    unsigned Opc = I->getOpcode();

    // Handle unconditional branch
    if (Opc == TMS9900::JMP) {
      if (!AllowModify) {
        TBB = I->getOperand(0).getMBB();
        continue;
      }

      // If the block has any instructions after a JMP, delete them.
      MBB.erase(std::next(I), MBB.end());
      Cond.clear();
      FBB = nullptr;

      // Delete the JMP if it's equivalent to a fall-through.
      if (MBB.isLayoutSuccessor(I->getOperand(0).getMBB())) {
        TBB = nullptr;
        I->eraseFromParent();
        I = MBB.end();
        continue;
      }

      // TBB is used to indicate the unconditional destination.
      TBB = I->getOperand(0).getMBB();
      continue;
    }

    if (Opc == TMS9900::CMPBRrr || Opc == TMS9900::CMPBRri) {
      if (!Cond.empty())
        return true;

      FBB = TBB;
      TBB = I->getOperand(3).getMBB();
      Cond.push_back(MachineOperand::CreateImm(Opc));
      Cond.push_back(I->getOperand(0));
      Cond.push_back(I->getOperand(1));
      Cond.push_back(I->getOperand(2));
      continue;
    }

    // Handle conditional branches
    if (!isCondBranchOpcode(Opc))
      return true;

    // Working from the bottom, handle the first conditional branch.
    if (Cond.empty()) {
      FBB = TBB;
      TBB = I->getOperand(0).getMBB();
      Cond.push_back(MachineOperand::CreateImm(Opc));
      continue;
    }

    // Handle subsequent conditional branches. Only handle the case where all
    // conditional branches branch to the same destination.
    if (TBB != I->getOperand(0).getMBB())
      return true;

    unsigned OldOpc = Cond[0].getImm();
    if (OldOpc == Opc)
      continue;

    return true;
  }

  bool CanInvert = false;
  if (!Cond.empty() && Cond[0].isImm()) {
    SmallVector<MachineOperand, 4> CondCopy(Cond.begin(), Cond.end());
    CanInvert = !reverseBranchCondition(CondCopy);
  }

  if (!AllowModify && !Cond.empty() && !FBB && TBB && MBB.succ_size() == 2 &&
      MBB.isLayoutSuccessor(TBB) && CanInvert) {
    for (MachineBasicBlock *Succ : MBB.successors()) {
      if (Succ != TBB) {
        FBB = Succ;
        break;
      }
    }
  }

  return false;
}

unsigned TMS9900InstrInfo::insertBranch(MachineBasicBlock &MBB,
                                         MachineBasicBlock *TBB,
                                         MachineBasicBlock *FBB,
                                         ArrayRef<MachineOperand> Cond,
                                         const DebugLoc &DL,
                                         int *BytesAdded) const {
  assert(TBB && "insertBranch must have a target");
  assert((Cond.size() == 0 || Cond.size() == 1 || Cond.size() == 4) &&
         "TMS9900 branch conditions have zero, one, or four components");

  if (BytesAdded)
    *BytesAdded = 0;

  if (Cond.empty()) {
    // Unconditional branch
    BuildMI(&MBB, DL, get(TMS9900::JMP)).addMBB(TBB);
    if (BytesAdded)
      *BytesAdded += 2;
    return 1;
  }

  unsigned Opc = Cond[0].getImm();

  if (Opc == TMS9900::CMPBRrr || Opc == TMS9900::CMPBRri) {
    assert(Cond.size() == 4 && "CMPBR expects 4 condition operands");
    MachineInstrBuilder MIB = BuildMI(&MBB, DL, get(Opc));
    MIB.add(Cond[1]); // LHS
    MIB.add(Cond[2]); // RHS
    MIB.add(Cond[3]); // CC
    MIB.addMBB(TBB);
    unsigned Count = 1;
    if (BytesAdded)
      *BytesAdded += 2;
    if (FBB) {
      BuildMI(&MBB, DL, get(TMS9900::JMP)).addMBB(FBB);
      if (BytesAdded)
        *BytesAdded += 2;
      ++Count;
    }
    return Count;
  }

  assert(isCondBranchOpcode(Opc) &&
         "invalid TMS9900 branch condition opcode");

  // Conditional branch
  unsigned Count = 0;
  BuildMI(&MBB, DL, get(Opc)).addMBB(TBB);
  if (BytesAdded)
    *BytesAdded += 2;
  ++Count;

  if (FBB) {
    // Two-way Conditional branch. Insert the second branch.
    BuildMI(&MBB, DL, get(TMS9900::JMP)).addMBB(FBB);
    if (BytesAdded)
      *BytesAdded += 2;
    ++Count;
  }

  return Count;
}

unsigned TMS9900InstrInfo::removeBranch(MachineBasicBlock &MBB,
                                         int *BytesRemoved) const {
  MachineBasicBlock::iterator I = MBB.end();
  unsigned Count = 0;

  while (I != MBB.begin()) {
    --I;

    if (I->isDebugInstr())
      continue;

    if (!I->isBranch())
      break;

    // Remove the branch
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }

  if (BytesRemoved)
    *BytesRemoved = Count * 2;  // Each branch is 2 bytes (may be wrong for some)

  return Count;
}

bool TMS9900InstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  if (Cond.empty() || !Cond[0].isImm())
    return true;

  unsigned Opc = Cond[0].getImm();
  if (Opc == TMS9900::CMPBRrr || Opc == TMS9900::CMPBRri) {
    if (Cond.size() != 4 || !Cond[3].isImm())
      return true;
    ISD::CondCode CC =
        static_cast<ISD::CondCode>(Cond[3].getImm());
    ISD::CondCode Inverted = ISD::getSetCCInverse(CC, MVT::i16);
    if (Inverted == ISD::SETCC_INVALID)
      return true;
    Cond[3].setImm(Inverted);
    return false;
  }

  if (Cond.size() != 1)
    return true;

  unsigned Inverted = getOppositeCondBranchOpcode(Opc);
  if (!Inverted)
    return true;

  Cond[0].setImm(Inverted);
  return false;
}

unsigned TMS9900InstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case TargetOpcode::CFI_INSTRUCTION:
  case TargetOpcode::EH_LABEL:
  case TargetOpcode::IMPLICIT_DEF:
  case TargetOpcode::KILL:
  case TargetOpcode::DBG_VALUE:
    return 0;
  case TargetOpcode::INLINEASM:
  case TargetOpcode::INLINEASM_BR: {
    const MachineFunction &MF = *MI.getParent()->getParent();
    return getInlineAsmLength(MI.getOperand(0).getSymbolName(),
                              *MF.getTarget().getMCAsmInfo());
  }
  default:
    return get(MI.getOpcode()).getSize();
  }
}

bool TMS9900InstrInfo::isBranchOffsetInRange(unsigned BranchOpc,
                                              int64_t BrOffset) const {
  switch (BranchOpc) {
  case TMS9900::JMP:
  case TMS9900::JEQ:
  case TMS9900::JNE:
  case TMS9900::JGT:
  case TMS9900::JLT:
  case TMS9900::JH:
  case TMS9900::JHE:
  case TMS9900::JL:
  case TMS9900::JLE:
  case TMS9900::JOC:
  case TMS9900::JNC:
  case TMS9900::JNO:
  case TMS9900::JOP: {
    if (BrOffset & 1)
      return false;
    int64_t WordOffset = (BrOffset >> 1) - 1;
    return isInt<8>(WordOffset);
  }
  case TMS9900::B_sym:
    return true;
  default:
    llvm_unreachable("unknown branch opcode");
  }
}

MachineBasicBlock *
TMS9900InstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();
  if (Opc == TMS9900::CMPBRrr || Opc == TMS9900::CMPBRri)
    return MI.getOperand(3).getMBB();
  if (Opc == TMS9900::JMP || Opc == TMS9900::B_sym || isCondBranchOpcode(Opc))
    return MI.getOperand(0).getMBB();
  return nullptr;
}

void TMS9900InstrInfo::insertIndirectBranch(MachineBasicBlock &MBB,
                                            MachineBasicBlock &NewDestBB,
                                            MachineBasicBlock &RestoreBB,
                                            const DebugLoc &DL,
                                            int64_t BrOffset,
                                            RegScavenger *RS) const {
  (void)RestoreBB;
  (void)BrOffset;
  (void)RS;
  BuildMI(&MBB, DL, get(TMS9900::B_sym)).addMBB(&NewDestBB);
}

bool TMS9900InstrInfo::isReallyTriviallyReMaterializable(
    const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  default:
    // Fall through to the base class for anything we don't explicitly handle.
    // The base class will reject instructions that def physical registers
    // (like ST), so only instructions listed here can be rematerialized.
    return false;

  case TMS9900::LI:
  case TMS9900::CLRr:
  case TMS9900::SETOr:
    // These produce constant values and have no memory operands.
    // They only define ST (status flags) as a side effect, which is safe
    // to re-execute since the flags are not a preserved resource.
    return true;

  case TMS9900::MOVam: {
    // MOV @addr,Rd - Load from a symbolic (absolute) address.
    // Safe to rematerialize if the memory is dereferenceable and invariant
    // (e.g., constant pool, read-only global). The isDereferenceableInvariantLoad
    // check examines the MachineMemOperands attached to this instruction.
    return MI.isDereferenceableInvariantLoad();
  }

  case TMS9900::MOVxm: {
    // MOV @offset(Ri),Rd - Load from indexed address.
    // Only safe to rematerialize if the base register is R10 (stack pointer)
    // and the memory location is dereferenceable and invariant (e.g., an
    // incoming argument on the stack that won't be modified).
    // Operands: $rd (def), $offset (imm), $ri (index reg)
    if (MI.getNumOperands() < 3)
      return false;
    const MachineOperand &BaseReg = MI.getOperand(2);
    if (!BaseReg.isReg() || BaseReg.getReg() != TMS9900::R10)
      return false;
    return MI.isDereferenceableInvariantLoad();
  }

  case TMS9900::MOV_FI_Load: {
    // MOV @offset(Ri),Rd - Frame index load pseudo.
    // Only safe to rematerialize if the memory is dereferenceable and invariant
    // (e.g., loading an immutable argument from the stack).
    // Operands: $rd (def), $base (reg), $offset (imm)
    return MI.isDereferenceableInvariantLoad();
  }
  }
}

bool TMS9900InstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();

  switch (MI.getOpcode()) {
  default:
    return false;
  case TMS9900::CMPBRrr:
  case TMS9900::CMPBRri: {
    bool IsImm = (MI.getOpcode() == TMS9900::CMPBRri);
    Register LHS = MI.getOperand(0).getReg();
    MachineBasicBlock *Target = MI.getOperand(3).getMBB();
    ISD::CondCode CC =
        static_cast<ISD::CondCode>(MI.getOperand(2).getImm());

    if (IsImm) {
      int64_t Imm = MI.getOperand(1).getImm();
      BuildMI(MBB, MI, DL, get(TMS9900::CI)).addReg(LHS).addImm(Imm);
    } else {
      Register RHS = MI.getOperand(1).getReg();
      BuildMI(MBB, MI, DL, get(TMS9900::Crr)).addReg(LHS).addReg(RHS);
    }

    if (CC == ISD::SETGE || CC == ISD::SETLE) {
      unsigned SecondOpc = (CC == ISD::SETGE) ? TMS9900::JGT : TMS9900::JLT;
      BuildMI(MBB, MI, DL, get(TMS9900::JEQ)).addMBB(Target);
      BuildMI(MBB, MI, DL, get(SecondOpc)).addMBB(Target);
    } else {
      unsigned JumpOpc = getJumpOpcodeForCC(CC);
      BuildMI(MBB, MI, DL, get(JumpOpc)).addMBB(Target);
    }

    MBB.erase(MI);
    return true;
  }
  case TMS9900::RET:
    // Expand RET pseudo to B *R11
    BuildMI(MBB, MI, DL, get(TMS9900::RET_REAL));
    MBB.erase(MI);
    return true;
  case TMS9900::TCRETURN:
  case TMS9900::TCRETURN_ext: {
    // Expand TCRETURN/TCRETURN_ext pseudo to B @target (tail call)
    // Uses TAIL_B instruction which takes a calltarget operand.
    MachineOperand &Target = MI.getOperand(0);
    BuildMI(MBB, MI, DL, get(TMS9900::TAIL_B))
        .add(Target);
    MBB.erase(MI);
    return true;
  }
  case TMS9900::TCRETURN_ind: {
    // Expand TCRETURN_ind pseudo to B *Rx (branch indirect through register)
    Register TargetReg = MI.getOperand(0).getReg();
    BuildMI(MBB, MI, DL, get(TMS9900::Br))
        .addReg(TargetReg);
    MBB.erase(MI);
    return true;
  }
  case TMS9900::ANDrr: {
    // Expand ANDrr pseudo to INV+SZC+INV sequence
    // AND rd, rs2 becomes:
    //   INV rs2      ; rs2 = NOT rs2
    //   SZC rs2, rd  ; rd = rd AND (NOT rs2) = rd AND (NOT (NOT original_rs2)) = rd AND original_rs2
    //   INV rs2      ; restore rs2 (skipped if rs2 is killed here)
    Register DstReg = MI.getOperand(0).getReg();
    Register SrcReg = MI.getOperand(2).getReg();
    // Use isKill() not isDead(): operand 2 is a use, not a def.
    // isKill() indicates the source register's last use is this instruction,
    // meaning no restore INV is needed. isDead() is for def operands only
    // and would always return false here.
    bool SrcIsKilled = MI.getOperand(2).isKill();

    // INV rs2
    BuildMI(MBB, MI, DL, get(TMS9900::INVr), SrcReg)
        .addReg(SrcReg);
    // SZC rs2, rd
    BuildMI(MBB, MI, DL, get(TMS9900::SZCrr), DstReg)
        .addReg(DstReg)
        .addReg(SrcReg);
    // INV rs2 (restore) — only needed if rs2 is still live after this AND
    if (!SrcIsKilled) {
      BuildMI(MBB, MI, DL, get(TMS9900::INVr), SrcReg)
          .addReg(SrcReg);
    }

    MBB.erase(MI);
    return true;
  }
  }
}
