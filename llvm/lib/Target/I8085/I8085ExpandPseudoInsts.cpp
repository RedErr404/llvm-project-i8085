//===-- I8085ExpandPseudoInsts.cpp - Expand pseudo instructions -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that expands pseudo instructions into target
// instructions. This pass should be run after register allocation but before
// the post-regalloc scheduling pass.
//
//===----------------------------------------------------------------------===//

#include "I8085.h"
#include "I8085InstrInfo.h"
#include "I8085TargetMachine.h"
#include "MCTargetDesc/I8085MCTargetDesc.h"

#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include <stdint.h>

using namespace llvm;

#define I8085_EXPAND_PSEUDO_NAME "I8085 pseudo instruction expansion pass"

namespace {

/// Expands "placeholder" instructions marked as pseudo into
/// actual I8085 instructions.
class I8085ExpandPseudo : public MachineFunctionPass {
public:
  static char ID;

  I8085ExpandPseudo() : MachineFunctionPass(ID) {
    initializeI8085ExpandPseudoPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return I8085_EXPAND_PSEUDO_NAME; }

private:
  typedef MachineBasicBlock Block;
  typedef Block::iterator BlockIt;

  const I8085RegisterInfo *TRI;
  const TargetInstrInfo *TII;
  bool HasUndoc;


  bool expandMBB(Block &MBB);
  bool expandMI(Block &MBB, BlockIt MBBI);
  template <unsigned OP> bool expand(Block &MBB, BlockIt MBBI);
  BlockIt nextNonDebugInstr(Block &MBB, BlockIt MBBI) const;
  bool tryExpandAdjacentLoad16Pair(Block &MBB, BlockIt MBBI);
  bool tryExpandAdjacentStore16Pair(Block &MBB, BlockIt MBBI);

  MachineInstrBuilder buildMI(Block &MBB, BlockIt MBBI, unsigned Opcode) {
    return BuildMI(MBB, MBBI, MBBI->getDebugLoc(), TII->get(Opcode));
  }

  MachineInstrBuilder buildMI(Block &MBB, BlockIt MBBI, unsigned Opcode,
                              Register DstReg) {
    return BuildMI(MBB, MBBI, MBBI->getDebugLoc(), TII->get(Opcode), DstReg);
  }

  void pushPSW(Block &MBB, BlockIt MBBI, bool CanClobberFlags) {
    if (CanClobberFlags)
      buildMI(MBB, MBBI, I8085::ORI).addImm(0);
    buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::PSW);
  }

  // Move the GR16 address operand into HL (unless it already is HL), then emit
  // incDecOpc (INR_M / DCR_M) which read-modify-writes the byte at (HL).
  // Defined out-of-line below (needs the file-scope getPairRegs helper).
  bool expandMemIncDecContent(Block &MBB, BlockIt MBBI, unsigned incDecOpc);

  MachineRegisterInfo &getRegInfo(Block &MBB) {
    return MBB.getParent()->getRegInfo();
  }

  bool hasAvailablePhysRegValue(Block &MBB, BlockIt MBBI, Register Reg) const {
    if (MBB.isLiveIn(Reg))
      return true;

    for (auto I = MBBI; I != MBB.begin();) {
      --I;
      for (const MachineOperand &MO : I->operands()) {
        if (!MO.isReg() || !MO.isDef())
          continue;
        Register DefReg = MO.getReg();
        if (DefReg == Reg || TRI->isSubRegisterEq(DefReg, Reg) ||
            TRI->isSubRegisterEq(Reg, DefReg))
          return true;
      }
    }

    return false;
  }

  bool hasAvailableHLValue(Block &MBB, BlockIt MBBI) const {
    return hasAvailablePhysRegValue(MBB, MBBI, I8085::HL) ||
           hasAvailablePhysRegValue(MBB, MBBI, I8085::H) ||
           hasAvailablePhysRegValue(MBB, MBBI, I8085::L);
  }

  bool isPhysRegLive(Block &MBB, BlockIt MBBI, Register Reg) const {
    LivePhysRegs LiveRegs(*TRI);
    LiveRegs.addLiveOuts(MBB);
    for (auto I = MBB.rbegin(), E = MBBI.getReverse(); I != E; ++I)
      LiveRegs.stepBackward(*I);
    return LiveRegs.contains(Reg);
  }

  bool isHLOrSubRegLive(Block &MBB, BlockIt MBBI) const {
    if (isPhysRegLive(MBB, MBBI, I8085::HL) ||
        isPhysRegLive(MBB, MBBI, I8085::H) ||
        isPhysRegLive(MBB, MBBI, I8085::L)) {
      return true;
    }
    if (MBB.isLiveIn(I8085::HL) || MBB.isLiveIn(I8085::H) ||
        MBB.isLiveIn(I8085::L))
      return true;

    if (!hasAvailableHLValue(MBB, MBBI))
      return false;

    // Do a forward scan from MBBI to detect if $h or $l is read before
    // being redefined.  This catches cases where the backwards walk might
    // miss liveness due to intervening definitions.  Skip over unexpanded
    // pseudo instructions that will preserve HL via PUSH/POP when expanded
    // (they also call isHLOrSubRegLive and will protect HL if it turns out
    // to be live).  Note: these pseudos no longer declare HL in Defs, so
    // the skip is mostly a safety net.
    for (auto I = std::next(MBBI), E = MBB.end(); I != E; ++I) {
      const MachineInstr &Inst = *I;
      // Check uses first: if H or L is used, HL is live.
      for (const MachineOperand &MO : Inst.operands()) {
        if (!MO.isReg() || !MO.isUse())
          continue;
        Register Reg = MO.getReg();
        if (Reg == I8085::H || Reg == I8085::L || Reg == I8085::HL)
          return true;
      }
      // Check defs: if H, L, or HL is defined, the current value in HL
      // may be dead -- but only if this is a real instruction or a pseudo
      // that unconditionally clobbers HL.  Pseudos that use the
      // PreserveHL pattern (PUSH/POP HL when live) will protect the value,
      // so we skip them and keep scanning.
      bool defsHL = false;
      for (const MachineOperand &MO : Inst.operands()) {
        if (!MO.isReg() || !MO.isDef())
          continue;
        Register Reg = MO.getReg();
        if (Reg == I8085::H || Reg == I8085::L || Reg == I8085::HL) {
          defsHL = true;
          break;
        }
      }
      if (defsHL) {
        // These pseudos all use isHLOrSubRegLive + PUSH/POP HL in their
        // expansion.  When they are expanded (later in this pass), they
        // will preserve HL if it is live.  So their implicit-def $hl
        // does not genuinely kill the HL value -- skip them.
        unsigned Opc = Inst.getOpcode();
        if (Opc == I8085::LOAD_8_WITH_ADDR ||
            Opc == I8085::LOAD_16_WITH_ADDR ||
            Opc == I8085::STORE_8 ||
            Opc == I8085::STORE_16 ||
            Opc == I8085::STORE_8_AT_OFFSET_WITH_SP ||
            Opc == I8085::STORE_16_AT_OFFSET_WITH_SP)
          continue;  // skip this pseudo, keep scanning
        // Otherwise, HL is genuinely redefined; stop scanning.
        return false;
      }
    }
    return false;
  }

  bool shouldPreservePSW(Block &MBB, BlockIt MBBI) const {
    return isPhysRegLive(MBB, MBBI, I8085::A) ||
           isPhysRegLive(MBB, MBBI, I8085::SREG);
  }

};

char I8085ExpandPseudo::ID = 0;

I8085ExpandPseudo::BlockIt
I8085ExpandPseudo::nextNonDebugInstr(Block &MBB, BlockIt MBBI) const {
  while (MBBI != MBB.end() && MBBI->isDebugInstr())
    ++MBBI;
  return MBBI;
}

static bool getPairRegs(unsigned Pair, unsigned &LowReg, unsigned &HighReg) {
  switch (Pair) {
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

static void addAddrOperand(MachineInstrBuilder &MIB,
                           const MachineOperand &MO,
                           int64_t Offset = 0) {
  switch (MO.getType()) {
  case MachineOperand::MO_Immediate:
    MIB.addImm(MO.getImm() + Offset);
    break;
  case MachineOperand::MO_GlobalAddress:
    MIB.addGlobalAddress(MO.getGlobal(), MO.getOffset() + Offset,
                         MO.getTargetFlags());
    break;
  case MachineOperand::MO_ExternalSymbol:
    assert((MO.getOffset() + Offset) == 0 &&
           "external symbol does not support offsets");
    MIB.addExternalSymbol(MO.getSymbolName(), MO.getTargetFlags());
    break;
  case MachineOperand::MO_ConstantPoolIndex:
    MIB.addConstantPoolIndex(MO.getIndex(), MO.getOffset() + Offset,
                             MO.getTargetFlags());
    break;
  case MachineOperand::MO_JumpTableIndex:
    assert((MO.getOffset() + Offset) == 0 &&
           "jump table operand does not support offsets");
    MIB.addJumpTableIndex(MO.getIndex(), MO.getTargetFlags());
    break;
  case MachineOperand::MO_BlockAddress:
    MIB.addBlockAddress(MO.getBlockAddress(), MO.getOffset() + Offset,
                        MO.getTargetFlags());
    break;
  default:
    llvm_unreachable("unexpected address operand type");
  }
}

bool I8085ExpandPseudo::tryExpandAdjacentLoad16Pair(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  auto NextIt = nextNonDebugInstr(MBB, std::next(MBBI));
  if (NextIt == MBB.end() || NextIt->getOpcode() != I8085::LOAD_16_WITH_ADDR)
    return false;

  MachineInstr &NextMI = *NextIt;
  unsigned DestA = MI.getOperand(0).getReg();
  unsigned BaseA = MI.getOperand(1).getReg();
  int64_t OffA = MI.getOperand(2).getImm();
  unsigned DestB = NextMI.getOperand(0).getReg();
  unsigned BaseB = NextMI.getOperand(1).getReg();
  int64_t OffB = NextMI.getOperand(2).getImm();

  if (BaseA != I8085::SP || BaseB != BaseA)
    return false;
  if (DestA == I8085::HL || DestA == I8085::SP || DestB == I8085::HL ||
      DestB == I8085::SP)
    return false;
  if (DestA == DestB)
    return false;
  if (isHLOrSubRegLive(MBB, MBBI) || isPhysRegLive(MBB, MBBI, I8085::SREG))
    return false;

  unsigned LowA = 0, HighA = 0, LowB = 0, HighB = 0;
  if (!getPairRegs(DestA, LowA, HighA) || !getPairRegs(DestB, LowB, HighB))
    return false;

  const bool FirstIsLower = OffA <= OffB;
  unsigned LowRegLow = FirstIsLower ? LowA : LowB;
  unsigned LowRegHigh = FirstIsLower ? HighA : HighB;
  unsigned HighRegLow = FirstIsLower ? LowB : LowA;
  unsigned HighRegHigh = FirstIsLower ? HighB : HighA;
  const int64_t LowOff = FirstIsLower ? OffA : OffB;
  const int64_t HighOff = FirstIsLower ? OffB : OffA;

  if (HighOff - LowOff < 2 || HighOff - LowOff > 4)
    return false;

  buildMI(MBB, MBBI, I8085::LXI)
      .addReg(I8085::HL, RegState::Define)
      .addImm(LowOff);
  buildMI(MBB, MBBI, I8085::DAD).addReg(I8085::SP);
  buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(LowRegLow, RegState::Define);
  buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);
  buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(LowRegHigh, RegState::Define);

  for (int64_t Step = LowOff + 1; Step < HighOff; ++Step)
    buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);

  buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(HighRegLow, RegState::Define);
  buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);
  buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(HighRegHigh, RegState::Define);

  NextMI.eraseFromParent();
  MI.eraseFromParent();
  return true;
}

bool I8085ExpandPseudo::tryExpandAdjacentStore16Pair(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  auto NextIt = nextNonDebugInstr(MBB, std::next(MBBI));
  if (NextIt == MBB.end() || NextIt->getOpcode() != I8085::STORE_16)
    return false;

  MachineInstr &NextMI = *NextIt;
  unsigned BaseA = MI.getOperand(0).getReg();
  int64_t OffA = MI.getOperand(1).getImm();
  unsigned SrcA = MI.getOperand(2).getReg();
  unsigned BaseB = NextMI.getOperand(0).getReg();
  int64_t OffB = NextMI.getOperand(1).getImm();
  unsigned SrcB = NextMI.getOperand(2).getReg();

  if (BaseA != I8085::SP || BaseB != BaseA)
    return false;
  if (SrcA == I8085::HL || SrcA == I8085::SP || SrcB == I8085::HL ||
      SrcB == I8085::SP)
    return false;
  if (isHLOrSubRegLive(MBB, MBBI) || isPhysRegLive(MBB, MBBI, I8085::SREG))
    return false;

  unsigned LowA = 0, HighA = 0, LowB = 0, HighB = 0;
  if (!getPairRegs(SrcA, LowA, HighA) || !getPairRegs(SrcB, LowB, HighB))
    return false;

  const bool FirstIsLower = OffA <= OffB;
  unsigned LowRegLow = FirstIsLower ? LowA : LowB;
  unsigned LowRegHigh = FirstIsLower ? HighA : HighB;
  unsigned HighRegLow = FirstIsLower ? LowB : LowA;
  unsigned HighRegHigh = FirstIsLower ? HighB : HighA;
  const int64_t LowOff = FirstIsLower ? OffA : OffB;
  const int64_t HighOff = FirstIsLower ? OffB : OffA;

  if (HighOff - LowOff < 2 || HighOff - LowOff > 4)
    return false;

  buildMI(MBB, MBBI, I8085::LXI)
      .addReg(I8085::HL, RegState::Define)
      .addImm(LowOff);
  buildMI(MBB, MBBI, I8085::DAD).addReg(I8085::SP);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(LowRegLow);
  buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(LowRegHigh);

  for (int64_t Step = LowOff + 1; Step < HighOff; ++Step)
    buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);

  buildMI(MBB, MBBI, I8085::MOV_M).addReg(HighRegLow);
  buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(HighRegHigh);

  NextMI.eraseFromParent();
  MI.eraseFromParent();
  return true;
}

bool I8085ExpandPseudo::expandMBB(MachineBasicBlock &MBB) {
  for (BlockIt MBBI = MBB.begin(), E = MBB.end(); MBBI != E; ) {
    // Some expansions splice instructions into new blocks, which can invalidate
    // iterators. Restart the scan after any successful expansion.
    if (expandMI(MBB, MBBI))
      return true;
    MBBI = std::next(MBBI);
  }

  return false;
}

bool I8085ExpandPseudo::runOnMachineFunction(MachineFunction &MF) {
  bool Modified = false;

  const I8085Subtarget &STI = MF.getSubtarget<I8085Subtarget>();
  TRI = STI.getRegisterInfo();
  TII = STI.getInstrInfo();
  HasUndoc = STI.hasUndocumented();

  // We need to track liveness in order to use register scavenging.
  MF.getProperties().set(MachineFunctionProperties::Property::TracksLiveness);

  for (Block &MBB : MF) {
    bool ContinueExpanding = true;
    unsigned ExpandCount = 0;
    unsigned MaxExpansions = static_cast<unsigned>(MBB.size()) * 20;
    if (MaxExpansions < 500)
      MaxExpansions = 500;

    // Continue expanding the block until all pseudos are expanded.
    do {
      if (ExpandCount++ >= MaxExpansions)
        report_fatal_error("I8085 pseudo expand limit reached");

      bool BlockModified = expandMBB(MBB);
      Modified |= BlockModified;

      ContinueExpanding = BlockModified;
    } while (ContinueExpanding);
  }

  return Modified;
}

template <>
bool I8085ExpandPseudo::expand<I8085::LOAD_16_ADDR_CONTENT>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned srcLowReg,srcHighReg;
  unsigned destLowReg,destHighReg;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  if ((srcReg == I8085::BC || srcReg == I8085::DE) && destReg != srcReg) {
    bool DestIsSP = (destReg == I8085::SP);
    if (DestIsSP)
      destReg = I8085::HL;

    if (!getPairRegs(destReg, destLowReg, destHighReg))
      return false;

    const bool PreservePSW = shouldPreservePSW(MBB, MBBI);
    const bool CanClobberFlags =
        !isPhysRegLive(MBB, MBBI, I8085::SREG);
    if (PreservePSW)
      pushPSW(MBB, MBBI, CanClobberFlags);
    buildMI(MBB, MBBI, I8085::LDAX).addReg(srcReg);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(destLowReg, RegState::Define)
        .addReg(I8085::A);

    buildMI(MBB, MBBI, I8085::INX).addReg(srcReg, RegState::Define);
    buildMI(MBB, MBBI, I8085::LDAX).addReg(srcReg);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(destHighReg, RegState::Define)
        .addReg(I8085::A);

    if (srcReg != destReg)
      buildMI(MBB, MBBI, I8085::DCX).addReg(srcReg, RegState::Define);

    if (PreservePSW)
      buildMI(MBB, MBBI, I8085::POP).addReg(I8085::PSW, RegState::Define);
    if (DestIsSP)
      buildMI(MBB, MBBI, I8085::SPHL);

    MI.eraseFromParent();
    return true;
  }

  bool HaveSrc = getPairRegs(srcReg, srcLowReg, srcHighReg);
  if (!HaveSrc && srcReg == I8085::SP) {
    buildMI(MBB, MBBI, I8085::LXI)
        .addReg(I8085::HL, RegState::Define)
        .addImm(0);
    buildMI(MBB, MBBI, I8085::DAD).addReg(I8085::SP);
    srcLowReg = I8085::L;
    srcHighReg = I8085::H;
    HaveSrc = true;
  }

  if (!HaveSrc)
    return false;

  // Load via HL pointer.
  if (srcReg != I8085::HL && srcReg != I8085::SP) {
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::H, RegState::Define)
        .addReg(srcHighReg);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::L, RegState::Define)
        .addReg(srcLowReg);
  }

  if (destReg == I8085::HL || destReg == I8085::SP) {
    const bool PreservePSW = shouldPreservePSW(MBB, MBBI);
    const bool CanClobberFlags =
        !isPhysRegLive(MBB, MBBI, I8085::SREG);
    if (PreservePSW)
      pushPSW(MBB, MBBI, CanClobberFlags);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::A, RegState::Define);
    buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::H, RegState::Define);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::L, RegState::Define)
        .addReg(I8085::A);
    if (PreservePSW)
      buildMI(MBB, MBBI, I8085::POP).addReg(I8085::PSW, RegState::Define);
    if (destReg == I8085::SP)
      buildMI(MBB, MBBI, I8085::SPHL);
    MI.eraseFromParent();
    return true;
  }

  if (!getPairRegs(destReg, destLowReg, destHighReg))
    return false;

  buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(destLowReg, RegState::Define);
  buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);
  buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(destHighReg, RegState::Define);
  if (srcReg == I8085::HL)
    buildMI(MBB, MBBI, I8085::DCX).addReg(I8085::HL, RegState::Define);

  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::LOAD_8_ADDR_CONTENT>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned lowReg,highReg;
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  if (srcReg == I8085::BC || srcReg == I8085::DE) {
    const bool PreservePSW =
        (destReg != I8085::A) && shouldPreservePSW(MBB, MBBI);
    const bool CanClobberFlags =
        !isPhysRegLive(MBB, MBBI, I8085::SREG);
    if (PreservePSW)
      pushPSW(MBB, MBBI, CanClobberFlags);
    buildMI(MBB, MBBI, I8085::LDAX).addReg(srcReg);
    if (destReg != I8085::A) {
      buildMI(MBB, MBBI, I8085::MOV)
          .addReg(destReg, RegState::Define)
          .addReg(I8085::A);
    }
    if (PreservePSW)
      buildMI(MBB, MBBI, I8085::POP).addReg(I8085::PSW, RegState::Define);
    MI.eraseFromParent();
    return true;
  }

  bool HaveSrc = getPairRegs(srcReg, lowReg, highReg);
  if (!HaveSrc && srcReg == I8085::SP) {
    buildMI(MBB, MBBI, I8085::LXI)
        .addReg(I8085::HL, RegState::Define)
        .addImm(0);
    buildMI(MBB, MBBI, I8085::DAD).addReg(I8085::SP);
    lowReg = I8085::L;
    highReg = I8085::H;
    HaveSrc = true;
  }

  if (!HaveSrc)
    return false;

  if (srcReg != I8085::HL && srcReg != I8085::SP) {
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::H, RegState::Define)
        .addReg(highReg);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::L, RegState::Define)
        .addReg(lowReg);
  }
  buildMI(MBB, MBBI,  I8085::MOV_FROM_M).addReg(destReg ,RegState::Define);
  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::STORE_8_ADDR_CONTENT>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned addrReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  if (addrReg == I8085::BC || addrReg == I8085::DE) {
    const bool PreservePSW =
        (srcReg != I8085::A) && shouldPreservePSW(MBB, MBBI);
    const bool CanClobberFlags =
        !isPhysRegLive(MBB, MBBI, I8085::SREG);
    if (PreservePSW)
      pushPSW(MBB, MBBI, CanClobberFlags);
    if (srcReg != I8085::A) {
      buildMI(MBB, MBBI, I8085::MOV)
          .addReg(I8085::A, RegState::Define)
          .addReg(srcReg);
    }
    buildMI(MBB, MBBI, I8085::STAX).addReg(addrReg);
    if (PreservePSW)
      buildMI(MBB, MBBI, I8085::POP).addReg(I8085::PSW, RegState::Define);
    MI.eraseFromParent();
    return true;
  }

  unsigned lowReg, highReg;
  bool HaveAddr = getPairRegs(addrReg, lowReg, highReg);
  if (!HaveAddr && addrReg == I8085::SP) {
    buildMI(MBB, MBBI, I8085::LXI)
        .addReg(I8085::HL, RegState::Define)
        .addImm(0);
    buildMI(MBB, MBBI, I8085::DAD).addReg(I8085::SP);
    lowReg = I8085::L;
    highReg = I8085::H;
    HaveAddr = true;
  }

  if (!HaveAddr)
    return false;

  if (addrReg != I8085::HL && addrReg != I8085::SP) {
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::H, RegState::Define)
        .addReg(highReg);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::L, RegState::Define)
        .addReg(lowReg);
  }

  buildMI(MBB, MBBI, I8085::MOV_M).addReg(srcReg);

  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::STORE_16_ADDR_CONTENT>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned addrReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  if (addrReg == I8085::HL && srcReg == I8085::HL) {
    buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::BC);
    buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::HL);
    buildMI(MBB, MBBI, I8085::POP).addReg(I8085::BC, RegState::Define);
    buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::C);
    buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);
    buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::B);
    buildMI(MBB, MBBI, I8085::POP).addReg(I8085::BC, RegState::Define);
    MI.eraseFromParent();
    return true;
  }

  if ((addrReg == I8085::BC || addrReg == I8085::DE) && addrReg != srcReg) {
    unsigned srcLow, srcHigh;
    if (!getPairRegs(srcReg, srcLow, srcHigh))
      return false;

    const bool PreservePSW = shouldPreservePSW(MBB, MBBI);
    const bool CanClobberFlags =
        !isPhysRegLive(MBB, MBBI, I8085::SREG);
    if (PreservePSW)
      pushPSW(MBB, MBBI, CanClobberFlags);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::A, RegState::Define)
        .addReg(srcLow);
    buildMI(MBB, MBBI, I8085::STAX).addReg(addrReg);
    buildMI(MBB, MBBI, I8085::INX).addReg(addrReg, RegState::Define);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::A, RegState::Define)
        .addReg(srcHigh);
    buildMI(MBB, MBBI, I8085::STAX).addReg(addrReg);
    buildMI(MBB, MBBI, I8085::DCX).addReg(addrReg, RegState::Define);
    if (PreservePSW)
      buildMI(MBB, MBBI, I8085::POP).addReg(I8085::PSW, RegState::Define);

    MI.eraseFromParent();
    return true;
  }

  unsigned addrLow, addrHigh;
  bool HaveAddr = getPairRegs(addrReg, addrLow, addrHigh);
  if (!HaveAddr && addrReg == I8085::SP) {
    buildMI(MBB, MBBI, I8085::LXI)
        .addReg(I8085::HL, RegState::Define)
        .addImm(0);
    buildMI(MBB, MBBI, I8085::DAD).addReg(I8085::SP);
    addrLow = I8085::L;
    addrHigh = I8085::H;
    HaveAddr = true;
  }

  if (!HaveAddr)
    return false;

  unsigned srcLow, srcHigh;
  if (!getPairRegs(srcReg, srcLow, srcHigh))
    return false;

  if (addrReg != I8085::HL && addrReg != I8085::SP) {
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::H, RegState::Define)
        .addReg(addrHigh);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::L, RegState::Define)
        .addReg(addrLow);
  }

  buildMI(MBB, MBBI, I8085::MOV_M).addReg(srcLow);
  buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(srcHigh);
  if (addrReg == I8085::HL)
    buildMI(MBB, MBBI, I8085::DCX).addReg(I8085::HL, RegState::Define);

  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::CALL_INDIRECT>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  MachineFunction *MF = MBB.getParent();

  const BasicBlock *LLVMBB = MBB.getBasicBlock();
  MachineBasicBlock *ReturnMBB = MF->CreateMachineBasicBlock(LLVMBB);
  auto InsertPos = std::next(MBB.getIterator());
  MF->insert(InsertPos, ReturnMBB);
  // Renumber ALL blocks to avoid conflicts with existing block numbers.
  MF->RenumberBlocks();

  ReturnMBB->splice(ReturnMBB->begin(), &MBB, std::next(MBBI), MBB.end());
  ReturnMBB->transferSuccessorsAndUpdatePHIs(&MBB);
  MBB.addSuccessor(ReturnMBB);
  LivePhysRegs LiveRegs;
  computeAndAddLiveIns(LiveRegs, *ReturnMBB);

  buildMI(MBB, MBBI, I8085::LXI)
      .addReg(I8085::BC, RegState::Define)
      .addMBB(ReturnMBB);
  buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::BC);
  buildMI(MBB, MBBI, I8085::PCHL);

  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::STORE_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;


  unsigned srcReg = MI.getOperand(2).getReg();
  unsigned baseReg = MI.getOperand(0).getReg();
  int64_t offsetToStore = MI.getOperand(1).getImm();

  const bool PreserveHL = (baseReg != I8085::HL) &&
                          isHLOrSubRegLive(MBB, MBBI);
  const bool PreserveHLFromSP = PreserveHL && (baseReg == I8085::SP);

  // DAD clobbers carry.  When flags are live across this pseudo (e.g. a
  // register spill inserted between SUB and JC/JNC), we must preserve PSW
  // around the DAD.  The HL-path (INX/DCX) is flag-safe and needs no save.
  const bool UseDAD = (baseReg != I8085::HL);
  const bool PreservePSW = UseDAD && isPhysRegLive(MBB, MBBI, I8085::SREG);

  // If srcReg is H or L and we're going to clobber HL with LXI+DAD, we must
  // copy the source value to A before the LXI destroys it.  The register
  // allocator's coalescer can widen GR8NoHL to GR8 and assign H/L despite
  // the operand constraint.
  const bool SrcIsHL = (srcReg == I8085::H || srcReg == I8085::L) &&
                       (baseReg != I8085::HL);
  // When SrcIsHL, we need A as a temporary.  If A is live (or SREG is live),
  // we must preserve PSW.  Subsumes the existing PreservePSW logic.
  const bool NeedPSWSave = SrcIsHL
      ? (PreservePSW || isPhysRegLive(MBB, MBBI, I8085::A))
      : PreservePSW;

  if (PreserveHL) {
    buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::HL);
    if (PreserveHLFromSP)
      offsetToStore += 2;
  }

  if (NeedPSWSave) {
    buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::PSW);
    if (baseReg == I8085::SP)
      offsetToStore += 2;
  }

  // If the source is H or L, copy it to A now, before LXI clobbers HL.
  if (SrcIsHL)
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::A, RegState::Define)
        .addReg(srcReg);

  auto addOffsetToHL = [&](int64_t Offset) {
    if (Offset > 0) {
      for (int64_t i = 0; i < Offset; ++i)
        buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);
    } else if (Offset < 0) {
      for (int64_t i = 0; i < -Offset; ++i)
        buildMI(MBB, MBBI, I8085::DCX).addReg(I8085::HL, RegState::Define);
    }
  };

  if (baseReg == I8085::HL) {
    addOffsetToHL(offsetToStore);
  } else {
    buildMI(MBB, MBBI, I8085::LXI)
        .addReg(I8085::HL,RegState::Define)
        .addImm(offsetToStore);
    buildMI(MBB, MBBI, I8085::DAD)
        .addReg(baseReg);
  }

  /* Store the register value pointed by HL reg.
   * If SrcIsHL, the value has been moved to A; use A as the source.
   * Otherwise, restore PSW (A + flags) before the store if needed. */

  if (!SrcIsHL && NeedPSWSave)
    buildMI(MBB, MBBI, I8085::POP).addReg(I8085::PSW, RegState::Define);

  buildMI(MBB, MBBI, I8085::MOV_M)
      .addReg(SrcIsHL ? (unsigned)I8085::A : srcReg);

  // When SrcIsHL, restore PSW *after* the store (A held the source value).
  if (SrcIsHL && NeedPSWSave)
    buildMI(MBB, MBBI, I8085::POP).addReg(I8085::PSW, RegState::Define);

  if (baseReg == I8085::HL)
    addOffsetToHL(-offsetToStore);

  if (PreserveHL)
    buildMI(MBB, MBBI, I8085::POP).addReg(I8085::HL, RegState::Define);

  MI.eraseFromParent();
  return true;
}


template <>
bool I8085ExpandPseudo::expand<I8085::LOAD_16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  const MachineOperand &Src = MI.getOperand(1);

  MachineInstrBuilder MIB = buildMI(MBB, MBBI, I8085::LXI)
                                .addReg(destReg, RegState::Define);
  addAddrOperand(MIB, Src);

  MI.eraseFromParent();

  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::LOAD_8_WITH_IMM_ADDR>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();

  const MachineOperand &AddrMO = MI.getOperand(1);

  // LDA loads a byte from a fixed address straight into A in one 3-byte
  // instruction without touching HL, so use it when A is the destination.
  if (destReg == I8085::A) {
    MachineInstrBuilder Ld = buildMI(MBB, MBBI, I8085::LDA);
    addAddrOperand(Ld, AddrMO);
    MI.eraseFromParent();
    return true;
  }

  MachineInstrBuilder Addr = buildMI(MBB, MBBI, I8085::LXI)
                                 .addReg(I8085::HL, RegState::Define);
  addAddrOperand(Addr, AddrMO);
  buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(destReg,RegState::Define);

  MI.eraseFromParent();

  return true;
}


template <>
bool I8085ExpandPseudo::expand<I8085::LOAD_16_WITH_IMM_ADDR>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned lowReg,highReg;
  unsigned destReg = MI.getOperand(0).getReg();

  const MachineOperand &AddrMO = MI.getOperand(1);

  // LHLD loads 16 bits from a fixed address straight into HL in one 3-byte
  // instruction (16 states, no flags affected) - far cheaper than the two
  // LXI/MOV-M pairs the byte-wise path used. HL is in this pseudo's Defs so it
  // may be clobbered freely; move the loaded value out to BC/DE (or SPHL) when
  // that is the destination.
  MachineInstrBuilder Ld = buildMI(MBB, MBBI, I8085::LHLD);
  addAddrOperand(Ld, AddrMO);

  if (destReg == I8085::HL) {
    MI.eraseFromParent();
    return true;
  }
  if (destReg == I8085::SP) {
    buildMI(MBB, MBBI, I8085::SPHL);
    MI.eraseFromParent();
    return true;
  }

  if (!getPairRegs(destReg, lowReg, highReg))
    return false;
  buildMI(MBB, MBBI, I8085::MOV).addReg(lowReg, RegState::Define).addReg(I8085::L);
  buildMI(MBB, MBBI, I8085::MOV).addReg(highReg, RegState::Define).addReg(I8085::H);

  MI.eraseFromParent();

  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::STORE_8_WITH_IMM_ADDR>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned srcReg = MI.getOperand(1).getReg();
  const MachineOperand &AddrMO = MI.getOperand(0);

  // STA stores A to a fixed address in one 3-byte instruction without touching
  // HL, so use it when the value is already in A.
  if (srcReg == I8085::A) {
    MachineInstrBuilder St = buildMI(MBB, MBBI, I8085::STA);
    addAddrOperand(St, AddrMO);
    MI.eraseFromParent();
    return true;
  }

  MachineInstrBuilder Addr = buildMI(MBB, MBBI, I8085::LXI)
                                 .addReg(I8085::HL, RegState::Define);
  addAddrOperand(Addr, AddrMO);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(srcReg);

  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::STORE_16_WITH_IMM_ADDR>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned srcReg = MI.getOperand(1).getReg();
  const MachineOperand &AddrMO = MI.getOperand(0);

  // SHLD stores HL to a fixed address in one 3-byte instruction. HL is in this
  // pseudo's Defs, so when the value is not already in HL we copy it there
  // (without disturbing the BC/DE source, which may still be live) and store.
  if (srcReg != I8085::HL) {
    unsigned srcLow, srcHigh;
    if (!getPairRegs(srcReg, srcLow, srcHigh))
      return false;
    buildMI(MBB, MBBI, I8085::MOV).addReg(I8085::L, RegState::Define).addReg(srcLow);
    buildMI(MBB, MBBI, I8085::MOV).addReg(I8085::H, RegState::Define).addReg(srcHigh);
  }

  MachineInstrBuilder St = buildMI(MBB, MBBI, I8085::SHLD);
  addAddrOperand(St, AddrMO);

  MI.eraseFromParent();
  return true;
}


template <>
bool I8085ExpandPseudo::expand<I8085::STORE_16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  // Two nearby SP-relative stores can share one address setup and walk HL
  // forward, which is smaller than emitting two independent LXI/DAD pairs.
  if (tryExpandAdjacentStore16Pair(MBB, MBBI))
    return true;
  
  unsigned lowReg,highReg;
  unsigned baseReg = MI.getOperand(0).getReg();
  int64_t offsetToStore = MI.getOperand(1).getImm();
  unsigned destReg = MI.getOperand(2).getReg();

  auto addOffsetToHL = [&](int64_t Offset) {
    if (Offset > 0) {
      for (int64_t i = 0; i < Offset; ++i)
        buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);
    } else if (Offset < 0) {
      for (int64_t i = 0; i < -Offset; ++i)
        buildMI(MBB, MBBI, I8085::DCX).addReg(I8085::HL, RegState::Define);
    }
  };

  if (destReg == I8085::HL) {
    const bool SrcIsKill = MI.getOperand(2).isKill();
    // DAD clobbers carry.  Preserve PSW when flags are live.
    const bool UseDAD = (baseReg != I8085::HL);
    const bool PreservePSW16HL = UseDAD && isPhysRegLive(MBB, MBBI, I8085::SREG);
    int64_t addrOffset = offsetToStore;
    if (baseReg == I8085::SP)
      addrOffset += 4;

    buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::BC);
    buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::HL);

    if (PreservePSW16HL) {
      buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::PSW);
      if (baseReg == I8085::SP)
        addrOffset += 2;
    }

    if (baseReg == I8085::HL) {
      addOffsetToHL(addrOffset);
    } else {
      buildMI(MBB, MBBI, I8085::LXI)
          .addReg(I8085::HL, RegState::Define)
          .addImm(addrOffset);
      buildMI(MBB, MBBI, I8085::DAD).addReg(baseReg);
    }

    if (PreservePSW16HL)
      buildMI(MBB, MBBI, I8085::POP).addReg(I8085::PSW, RegState::Define);

    buildMI(MBB, MBBI, I8085::POP).addReg(I8085::BC, RegState::Define);
    buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::C);
    buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);
    buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::B);

    if (!SrcIsKill) {
      buildMI(MBB, MBBI, I8085::MOV)
          .addReg(I8085::H, RegState::Define)
          .addReg(I8085::B);
      buildMI(MBB, MBBI, I8085::MOV)
          .addReg(I8085::L, RegState::Define)
          .addReg(I8085::C);
    }

    buildMI(MBB, MBBI, I8085::POP).addReg(I8085::BC, RegState::Define);
    MI.eraseFromParent();
    return true;
  }

  const bool PreserveHL = (baseReg != I8085::HL) &&
                          isHLOrSubRegLive(MBB, MBBI);
  const bool PreserveHLFromSP = PreserveHL && (baseReg == I8085::SP);

  if (PreserveHL) {
    buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::HL);
    if (PreserveHLFromSP)
      offsetToStore += 2;
  }
  
  if (destReg == I8085::SP) {
    // DAD clobbers carry.  Preserve PSW when flags are live.
    // Note: PUSH PSW changes SP by -2, so we must compensate the read value.
    const bool PreservePSW16SP = isPhysRegLive(MBB, MBBI, I8085::SREG);
    if (PreservePSW16SP)
      buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::PSW);
    // LXI H, offset / DAD SP captures current SP into HL.
    // If we pushed PSW, SP is 2 lower, so add 2 to compensate.
    buildMI(MBB, MBBI, I8085::LXI)
        .addReg(I8085::HL, RegState::Define)
        .addImm(PreservePSW16SP ? 2 : 0);
    buildMI(MBB, MBBI, I8085::DAD).addReg(I8085::SP);
    if (PreservePSW16SP)
      buildMI(MBB, MBBI, I8085::POP).addReg(I8085::PSW, RegState::Define);
    lowReg = I8085::L;
    highReg = I8085::H;
  } else if (!getPairRegs(destReg, lowReg, highReg)) {
    return false;
  }

  if (baseReg == I8085::HL) {
    addOffsetToHL(offsetToStore);
    buildMI(MBB, MBBI, I8085::MOV_M).addReg(lowReg);
    buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);
    buildMI(MBB, MBBI, I8085::MOV_M).addReg(highReg);
    if (PreserveHL)
      buildMI(MBB, MBBI, I8085::POP).addReg(I8085::HL, RegState::Define);
    MI.eraseFromParent();
    return true;
  }

  if (destReg != I8085::SP) {
    // When the source is BC/DE and HL is free, store both bytes through one
    // computed address instead of two separate STORE_8 expansions.
    const bool PreservePSWCarry = isPhysRegLive(MBB, MBBI, I8085::SREG);
    if (PreservePSWCarry) {
      buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::PSW);
      if (baseReg == I8085::SP)
        offsetToStore += 2;
    }

    buildMI(MBB, MBBI, I8085::LXI)
        .addReg(I8085::HL, RegState::Define)
        .addImm(offsetToStore);
    buildMI(MBB, MBBI, I8085::DAD).addReg(baseReg);

    if (PreservePSWCarry)
      buildMI(MBB, MBBI, I8085::POP).addReg(I8085::PSW, RegState::Define);

    buildMI(MBB, MBBI, I8085::MOV_M).addReg(lowReg);
    buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);
    buildMI(MBB, MBBI, I8085::MOV_M).addReg(highReg);
  } else {
    buildMI(MBB, MBBI, I8085::STORE_8)
        .addReg(baseReg)
        .addImm(offsetToStore + 1)
        .addReg(highReg);

    buildMI(MBB, MBBI, I8085::STORE_8)
        .addReg(baseReg)
        .addImm(offsetToStore)
        .addReg(lowReg);
  }

  if (PreserveHL)
    buildMI(MBB, MBBI, I8085::POP).addReg(I8085::HL, RegState::Define);

  MI.eraseFromParent();

  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::SHRINK_STACK_BY>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  int64_t Amount = MI.getOperand(0).getImm();

  buildMI(MBB, MBBI,  I8085::LXI)
        .addReg(I8085::HL,RegState::Define)
        .addImm(Amount);

  buildMI(MBB, MBBI,  I8085::DAD)
        .addReg(I8085::SP);

  buildMI(MBB, MBBI,  I8085::SPHL);
  
  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::GROW_STACK_BY>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  int64_t AmountImm = MI.getOperand(0).getImm();
  uint16_t Amount = static_cast<uint16_t>(-AmountImm);
  
  buildMI(MBB, MBBI,  I8085::LXI)
        .addReg(I8085::HL,RegState::Define)
        .addImm(Amount);

  buildMI(MBB, MBBI,  I8085::DAD)
        .addReg(I8085::SP);

  buildMI(MBB, MBBI,  I8085::SPHL);
  
  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::LOAD_8_WITH_ADDR>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;


  unsigned destReg = MI.getOperand(0).getReg();
  unsigned baseReg = MI.getOperand(1).getReg();
  int64_t offsetToLoad = MI.getOperand(2).getImm();

  // Undoc fast path: LDSI + LDAX D (3 bytes) vs LXI H,offset; DAD SP; MOV r,M (5+ bytes).
  // No flag clobber, no HL clobber.
  if (HasUndoc && baseReg == I8085::SP && offsetToLoad >= 0 && offsetToLoad <= 255) {
    bool DEUsed = isPhysRegLive(MBB, MBBI, I8085::DE) ||
                  isPhysRegLive(MBB, MBBI, I8085::D) ||
                  isPhysRegLive(MBB, MBBI, I8085::E);
    bool DEOk = !DEUsed || destReg == I8085::D || destReg == I8085::E;
    if (DEOk) {
      buildMI(MBB, MBBI, I8085::LDSI).addImm(offsetToLoad);
      buildMI(MBB, MBBI, I8085::LDAX).addReg(I8085::DE);
      if (destReg != I8085::A)
        buildMI(MBB, MBBI, I8085::MOV).addReg(destReg, RegState::Define).addReg(I8085::A);
      MI.eraseFromParent();
      return true;
    }
  }

  // When destReg is H or L and the base requires LXI+DAD (base != HL),
  // the "other" sub-register of HL is clobbered by the address computation.
  // If that other sub-register is live, we must preserve it by loading via A
  // as a temporary, then POP H to restore the other half, then MOV dest, A.
  const bool DestIsH = (destReg == I8085::H);
  const bool DestIsL = (destReg == I8085::L);
  const bool DestIsSubHL = (DestIsH || DestIsL) && (baseReg != I8085::HL);
  // Check if the "other" sub-register is live.
  const bool OtherSubLive = DestIsSubHL &&
      (DestIsH ? (isPhysRegLive(MBB, MBBI, I8085::L))
               : (isPhysRegLive(MBB, MBBI, I8085::H)));

  const bool PreserveHL =
      OtherSubLive ||
      (baseReg != I8085::HL && !DestIsH && !DestIsL &&
       isHLOrSubRegLive(MBB, MBBI));
  const bool PreserveHLFromSP = PreserveHL && (baseReg == I8085::SP);
  const bool PreserveHLFromHL =
      (baseReg == I8085::HL && destReg != I8085::H && destReg != I8085::L);

  // DAD clobbers carry.  When flags are live across this pseudo (e.g. a
  // register reload inserted between SUB and JC/JNC), we must preserve PSW
  // around the DAD.  The HL-path (INX/DCX) is flag-safe and needs no save.
  const bool UseDAD = (baseReg != I8085::HL);
  const bool PreservePSW = UseDAD && isPhysRegLive(MBB, MBBI, I8085::SREG);

  // When OtherSubLive, we use A as a temp to shuttle the loaded value.
  // If A is also live (or SREG), we need PSW preservation.
  const bool NeedPSWSave = OtherSubLive
      ? (PreservePSW || isPhysRegLive(MBB, MBBI, I8085::A))
      : PreservePSW;

  // When OtherSubLive is true, we use A as a temp and need PUSH/POP in the
  // order: PUSH PSW, PUSH H ... POP H, MOV dest,A, POP PSW (LIFO).
  // When OtherSubLive is false, the normal path pops PSW right after DAD
  // and POP H at the end: PUSH H, PUSH PSW ... POP PSW, MOV dest,M, POP H.
  if (OtherSubLive) {
    // OtherSubLive path: push PSW first, then H (LIFO: pop H first, PSW last)
    if (NeedPSWSave) {
      buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::PSW);
      if (baseReg == I8085::SP)
        offsetToLoad += 2;
    }
    if (PreserveHL) {
      buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::HL);
      if (PreserveHLFromSP)
        offsetToLoad += 2;
    }
  } else {
    // Normal path: push H first, then PSW (LIFO: pop PSW first, H last)
    if (PreserveHL) {
      buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::HL);
      if (PreserveHLFromSP)
        offsetToLoad += 2;
    }
    if (NeedPSWSave) {
      buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::PSW);
      if (baseReg == I8085::SP)
        offsetToLoad += 2;
    }
  }

  auto addOffsetToHL = [&](int64_t Offset) {
    if (Offset > 0) {
      for (int64_t i = 0; i < Offset; ++i)
        buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);
    } else if (Offset < 0) {
      for (int64_t i = 0; i < -Offset; ++i)
        buildMI(MBB, MBBI, I8085::DCX).addReg(I8085::HL, RegState::Define);
    }
  };

  if (baseReg == I8085::HL) {
    addOffsetToHL(offsetToLoad);
  } else {
    buildMI(MBB, MBBI, I8085::LXI)
        .addReg(I8085::HL,RegState::Define)
        .addImm(offsetToLoad);
    buildMI(MBB, MBBI, I8085::DAD)
        .addReg(baseReg);
  }

  if (OtherSubLive) {
    // Load into A (temp), restore HL (gets back the other sub-reg),
    // move A into dest, then restore PSW.
    // Stack order (LIFO): PSW was pushed first, H second.
    // So POP H comes first, POP PSW comes last.
    buildMI(MBB, MBBI, I8085::MOV_FROM_M)
        .addReg(I8085::A, RegState::Define);

    buildMI(MBB, MBBI, I8085::POP).addReg(I8085::HL, RegState::Define);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(destReg, RegState::Define)
        .addReg(I8085::A);

    if (NeedPSWSave)
      buildMI(MBB, MBBI, I8085::POP).addReg(I8085::PSW, RegState::Define);
  } else {
    // Normal path: restore PSW after DAD, then load directly into destReg.
    if (NeedPSWSave)
      buildMI(MBB, MBBI, I8085::POP).addReg(I8085::PSW, RegState::Define);

    buildMI(MBB, MBBI, I8085::MOV_FROM_M)
        .addReg(destReg, RegState::Define);

    if (baseReg == I8085::HL && PreserveHLFromHL)
      addOffsetToHL(-offsetToLoad);

    if (PreserveHL)
      buildMI(MBB, MBBI, I8085::POP).addReg(I8085::HL, RegState::Define);
  }

  MI.eraseFromParent();
  return true;
}


template <>
bool I8085ExpandPseudo::expand<I8085::LOAD_16_WITH_ADDR>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  // Likewise for two nearby SP-relative loads when HL and flags are already
  // free: one computed address plus a short HL walk beats a second LXI/DAD.
  // When undoc is enabled, LDSI+LHLX per load is smaller and avoids carry clobber.
  if (!HasUndoc && tryExpandAdjacentLoad16Pair(MBB, MBBI))
    return true;
  
  unsigned lowReg,highReg;
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned baseReg = MI.getOperand(1).getReg();
  int64_t offsetToLoad = MI.getOperand(2).getImm();

  // Undoc fast path: LDSI + LHLX for SP-relative 16-bit loads.
  // LDSI offset ; LHLX = 3 bytes, no flag clobber.
  // Standard: LXI H,offset; DAD SP; MOV A,M; INX H; MOV H,M; MOV L,A = 8+ bytes.
  // Requires: base=SP, offset in [0,255], DE is dead or being written by this load.
  if (HasUndoc && baseReg == I8085::SP && offsetToLoad >= 0 && offsetToLoad <= 255) {
    bool DEUsed = isPhysRegLive(MBB, MBBI, I8085::DE) ||
                  isPhysRegLive(MBB, MBBI, I8085::D) ||
                  isPhysRegLive(MBB, MBBI, I8085::E);
    bool DEOk = !DEUsed || destReg == I8085::DE;
    if (DEOk) {
    if (destReg == I8085::HL || destReg == I8085::SP) {
      buildMI(MBB, MBBI, I8085::LDSI).addImm(offsetToLoad);
      buildMI(MBB, MBBI, I8085::LHLX);
      if (destReg == I8085::SP)
        buildMI(MBB, MBBI, I8085::SPHL);
      MI.eraseFromParent();
      return true;
    }
    if (getPairRegs(destReg, lowReg, highReg)) {
      buildMI(MBB, MBBI, I8085::LDSI).addImm(offsetToLoad);
      buildMI(MBB, MBBI, I8085::LHLX);
      buildMI(MBB, MBBI, I8085::MOV).addReg(lowReg, RegState::Define).addReg(I8085::L);
      buildMI(MBB, MBBI, I8085::MOV).addReg(highReg, RegState::Define).addReg(I8085::H);
      MI.eraseFromParent();
      return true;
    }
    }
  }

  const bool PreserveHL =
      (baseReg != I8085::HL && destReg != I8085::HL &&
       destReg != I8085::SP && isHLOrSubRegLive(MBB, MBBI));
  const bool PreserveHLFromSP = PreserveHL && (baseReg == I8085::SP);
  const bool PreservePSW =
      (destReg == I8085::HL || destReg == I8085::SP) &&
      shouldPreservePSW(MBB, MBBI);

  if (PreserveHL) {
    buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::HL);
    if (PreserveHLFromSP)
      offsetToLoad += 2;
  }
  if (PreservePSW) {
    const bool CanClobberFlags =
        !isPhysRegLive(MBB, MBBI, I8085::SREG);
    pushPSW(MBB, MBBI, CanClobberFlags);
    if (baseReg == I8085::SP)
      offsetToLoad += 2;
  }
  
  auto addOffsetToHL = [&](int64_t Offset) {
    if (Offset > 0) {
      for (int64_t i = 0; i < Offset; ++i)
        buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);
    } else if (Offset < 0) {
      for (int64_t i = 0; i < -Offset; ++i)
        buildMI(MBB, MBBI, I8085::DCX).addReg(I8085::HL, RegState::Define);
    }
  };

  if (destReg == I8085::HL || destReg == I8085::SP) {
    if (baseReg == I8085::HL) {
      addOffsetToHL(offsetToLoad);
    } else {
      buildMI(MBB, MBBI, I8085::LXI)
          .addReg(I8085::HL,RegState::Define)
          .addImm(offsetToLoad);
      buildMI(MBB, MBBI, I8085::DAD)
          .addReg(baseReg);
    }

    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::A, RegState::Define);
    buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::H, RegState::Define);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::L, RegState::Define)
        .addReg(I8085::A);

    if (PreservePSW)
      buildMI(MBB, MBBI, I8085::POP).addReg(I8085::PSW, RegState::Define);

    if (destReg == I8085::SP)
      buildMI(MBB, MBBI, I8085::SPHL);
    MI.eraseFromParent();
    return true;
  }

  if (!getPairRegs(destReg, lowReg, highReg))
    return false;

  if (PreserveHL) {
    // DAD clobbers carry.  Preserve PSW when flags are live.
    const bool PreservePSWCarry = isPhysRegLive(MBB, MBBI, I8085::SREG);
    if (PreservePSWCarry) {
      buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::PSW);
      if (baseReg == I8085::SP)
        offsetToLoad += 2;
    }
    buildMI(MBB, MBBI, I8085::LXI)
        .addReg(I8085::HL, RegState::Define)
        .addImm(offsetToLoad);
    buildMI(MBB, MBBI, I8085::DAD)
        .addReg(baseReg);
    if (PreservePSWCarry)
      buildMI(MBB, MBBI, I8085::POP).addReg(I8085::PSW, RegState::Define);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(lowReg, RegState::Define);
    buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(highReg, RegState::Define);
    if (PreservePSW)
      buildMI(MBB, MBBI, I8085::POP).addReg(I8085::PSW, RegState::Define);
    buildMI(MBB, MBBI, I8085::POP).addReg(I8085::HL, RegState::Define);
    MI.eraseFromParent();
    return true;
  }

  if (baseReg == I8085::HL) {
    addOffsetToHL(offsetToLoad);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(lowReg, RegState::Define);
    buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(highReg, RegState::Define);
    MI.eraseFromParent();
    return true;
  }

  // When HL is already free, load both bytes through one computed address
  // instead of expanding into two separate 8-bit loads.
  const bool PreservePSWCarry = isPhysRegLive(MBB, MBBI, I8085::SREG);
  if (PreservePSWCarry) {
    buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::PSW);
    if (baseReg == I8085::SP)
      offsetToLoad += 2;
  }

  buildMI(MBB, MBBI, I8085::LXI)
      .addReg(I8085::HL, RegState::Define)
      .addImm(offsetToLoad);
  buildMI(MBB, MBBI, I8085::DAD).addReg(baseReg);

  if (PreservePSWCarry)
    buildMI(MBB, MBBI, I8085::POP).addReg(I8085::PSW, RegState::Define);

  buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(lowReg, RegState::Define);
  buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define);
  buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(highReg, RegState::Define);
  
  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::FRMIDX>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned baseReg = MI.getOperand(1).getReg();
  int16_t offset = MI.getOperand(2).getImm();

  // HL = base + offset
  buildMI(MBB, MBBI, I8085::LXI)
      .addReg(I8085::HL, RegState::Define)
      .addImm(offset);
  buildMI(MBB, MBBI, I8085::DAD)
      .addReg(baseReg);

  if (destReg == I8085::HL) {
    MI.eraseFromParent();
    return true;
  }

  if (destReg == I8085::SP) {
    buildMI(MBB, MBBI, I8085::SPHL);
    MI.eraseFromParent();
    return true;
  }

  unsigned lowReg = 0, highReg = 0;
  if (!getPairRegs(destReg, lowReg, highReg))
    return false;

  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(highReg, RegState::Define)
      .addReg(I8085::H);
  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(lowReg, RegState::Define)
      .addReg(I8085::L);

  MI.eraseFromParent();
  return true;
}


template <>
bool I8085ExpandPseudo::expand<I8085::ANDI_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operandOne = MI.getOperand(1).getReg();
  int64_t immValue = MI.getOperand(2).getImm();    
  
  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(I8085::A,RegState::Define)
      .addReg(operandOne);

  buildMI(MBB, MBBI, I8085::ANI)
      .addImm(immValue);

  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(destReg,RegState::Define)
      .addReg(I8085::A);

  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::ORI_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operandOne = MI.getOperand(1).getReg();
  int64_t immValue = MI.getOperand(2).getImm();    
  
  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(I8085::A,RegState::Define)
      .addReg(operandOne);

  buildMI(MBB, MBBI, I8085::ORI)
      .addImm(immValue);

  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(destReg,RegState::Define)
      .addReg(I8085::A);

  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::XORI_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operandOne = MI.getOperand(1).getReg();
  int64_t immValue = MI.getOperand(2).getImm();    
  
  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(I8085::A,RegState::Define)
      .addReg(operandOne);

  // ~x is xor x, 0xFF: use CMA (1 byte, 4T, sets no flags) instead of
  // XRI 0xFF (2 bytes, 7T, clobbers Z/S/P/CY/AC). Only when the flags this
  // pseudo defines are dead, since CMA leaves them untouched.
  if ((immValue & 0xFF) == 0xFF && MI.registerDefIsDead(I8085::SREG, TRI)) {
    buildMI(MBB, MBBI, I8085::CMA);
  } else {
    buildMI(MBB, MBBI, I8085::XRI)
        .addImm(immValue);
  }

  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(destReg,RegState::Define)
      .addReg(I8085::A);

  MI.eraseFromParent();
  return true;
}



template <>
bool I8085ExpandPseudo::expand<I8085::AND_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operandOne = MI.getOperand(1).getReg();
  uint16_t operandTwo = MI.getOperand(2).getReg();
  bool Op2IsA = (operandTwo == I8085::A);

  // Don't emit a COPY into destReg if it would clobber A when we need it.
  if (destReg != operandOne && !(Op2IsA && destReg == I8085::A)) {
    buildMI(MBB, MBBI, TargetOpcode::COPY, destReg)
        .addReg(operandOne);
  }

  // AND is commutative. If operandTwo is A, swap operands so that
  // MOV A,<x> doesn't clobber the value already in A.
  if (Op2IsA) {
    buildMI(MBB, MBBI, I8085::ANA)
        .addReg(operandOne);
  } else {
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::A, RegState::Define)
        .addReg(operandOne);

    buildMI(MBB, MBBI, I8085::ANA)
        .addReg(operandTwo);
  }

  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(destReg,RegState::Define)
      .addReg(I8085::A);

  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::OR_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operandOne = MI.getOperand(1).getReg();
  uint16_t operandTwo = MI.getOperand(2).getReg();

  // OR is commutative. If operandTwo is A, swap operands so that
  // MOV A,<x> doesn't clobber the value already in A.
  if (operandTwo == I8085::A) {
    buildMI(MBB, MBBI, I8085::ORA)
        .addReg(operandOne);
  } else {
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::A, RegState::Define)
        .addReg(operandOne);

    buildMI(MBB, MBBI, I8085::ORA)
        .addReg(operandTwo);
  }

  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(destReg,RegState::Define)
      .addReg(I8085::A);

  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::XOR_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operandOne = MI.getOperand(1).getReg();
  uint16_t operandTwo = MI.getOperand(2).getReg();

  // XOR is commutative. If operandTwo is A, swap operands so that
  // MOV A,<x> doesn't clobber the value already in A.
  if (operandTwo == I8085::A) {
    buildMI(MBB, MBBI, I8085::XRA)
        .addReg(operandOne);
  } else {
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::A, RegState::Define)
        .addReg(operandOne);

    buildMI(MBB, MBBI, I8085::XRA)
        .addReg(operandTwo);
  }

  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(destReg,RegState::Define)
      .addReg(I8085::A);

  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::AND_16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operandOne = MI.getOperand(1).getReg();
  uint16_t operandTwo = MI.getOperand(2).getReg();  
  bool DstIsDead = MI.getOperand(0).isDead();
  
  unsigned opLow,opHigh;
  unsigned destLow,destHigh;

  if (!getPairRegs(destReg, destLow, destHigh))
    return false;
  if (destReg != operandOne) {
    buildMI(MBB, MBBI, TargetOpcode::COPY, destReg)
        .addReg(operandOne);
  }
  if (!getPairRegs(operandTwo, opLow, opHigh))
    return false;
  
  buildMI(MBB, MBBI, I8085::AND_8)
      .addReg(destLow, RegState::Define | getDeadRegState(DstIsDead))
      .addReg(destLow)
      .addReg(opLow);

  buildMI(MBB, MBBI, I8085::AND_8)
      .addReg(destHigh, RegState::Define | getDeadRegState(DstIsDead))
      .addReg(destHigh)
      .addReg(opHigh);    

  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::OR_16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operandOne = MI.getOperand(1).getReg();
  uint16_t operandTwo = MI.getOperand(2).getReg();
  bool DstIsDead = MI.getOperand(0).isDead();
  
  unsigned opLow,opHigh;
  unsigned destLow,destHigh;

  if (!getPairRegs(destReg, destLow, destHigh))
    return false;
  if (!getPairRegs(operandTwo, opLow, opHigh))
    return false;
  
  buildMI(MBB, MBBI, I8085::OR_8)
      .addReg(destLow, RegState::Define | getDeadRegState(DstIsDead))
      .addReg(destLow)
      .addReg(opLow);

  buildMI(MBB, MBBI, I8085::OR_8)
      .addReg(destHigh, RegState::Define | getDeadRegState(DstIsDead))
      .addReg(destHigh)
      .addReg(opHigh);    
      
  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::XOR_16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operandOne = MI.getOperand(1).getReg();
  uint16_t operandTwo = MI.getOperand(2).getReg();
  bool DstIsDead = MI.getOperand(0).isDead();
  
  unsigned opLow,opHigh;
  unsigned destLow,destHigh;

  if (!getPairRegs(destReg, destLow, destHigh))
    return false;
  if (!getPairRegs(operandTwo, opLow, opHigh))
    return false;
  
  buildMI(MBB, MBBI, I8085::XOR_8)
      .addReg(destLow, RegState::Define | getDeadRegState(DstIsDead))
      .addReg(destLow)
      .addReg(opLow);

  buildMI(MBB, MBBI, I8085::XOR_8)
      .addReg(destHigh, RegState::Define | getDeadRegState(DstIsDead))
      .addReg(destHigh)
      .addReg(opHigh);

  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::NOT_16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();  // tied to destReg

  unsigned destLow, destHigh, srcLow, srcHigh;
  if (!getPairRegs(destReg, destLow, destHigh))
    return false;
  if (!getPairRegs(srcReg, srcLow, srcHigh))
    return false;

  // Complement each byte with CMA (1 byte, 4T, sets no flags).
  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(I8085::A, RegState::Define)
      .addReg(srcLow);
  buildMI(MBB, MBBI, I8085::CMA);
  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(destLow, RegState::Define)
      .addReg(I8085::A);

  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(I8085::A, RegState::Define)
      .addReg(srcHigh);
  buildMI(MBB, MBBI, I8085::CMA);
  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(destHigh, RegState::Define)
      .addReg(I8085::A);

  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::ADD_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operandOne = MI.getOperand(1).getReg();
  MachineOperand &Op2 = MI.getOperand(2);

  bool Op2IsImm = Op2.isImm();
  bool Op2IsA = !Op2IsImm && Op2.getReg() == I8085::A;

  // Only copy into the destination when we won't clobber an A-held operand.
  if (destReg != operandOne && !(Op2IsA && destReg == I8085::A)) {
    buildMI(MBB, MBBI, TargetOpcode::COPY, destReg)
        .addReg(operandOne);
  }

  if (Op2IsImm) {
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::A, RegState::Define)
        .addReg(destReg);
    buildMI(MBB, MBBI, I8085::ADI)
        .addImm(static_cast<uint8_t>(Op2.getImm()));
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(destReg, RegState::Define)
        .addReg(I8085::A);
  } else if (Op2IsA) {
    // A already holds the RHS; add the LHS directly.
    if (destReg == I8085::A) {
      buildMI(MBB, MBBI, I8085::ADD)
          .addReg(operandOne);
    } else {
      buildMI(MBB, MBBI, I8085::ADD)
          .addReg(destReg);
      buildMI(MBB, MBBI, I8085::MOV)
          .addReg(destReg, RegState::Define)
          .addReg(I8085::A);
    }
  } else {
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::A, RegState::Define)
        .addReg(destReg);
    buildMI(MBB, MBBI, I8085::ADD)
        .addReg(Op2.getReg());
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(destReg, RegState::Define)
        .addReg(I8085::A);
  }

  MI.eraseFromParent();
  return true;
}


template <>
bool I8085ExpandPseudo::expand<I8085::SUB_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operandOne = MI.getOperand(1).getReg();
  MachineOperand &Op2 = MI.getOperand(2);

  bool Op2IsImm = Op2.isImm();
  bool Op2IsA = !Op2IsImm && Op2.getReg() == I8085::A;

  if (Op2IsImm) {
    // Immediate case: straightforward
    if (destReg != operandOne) {
      buildMI(MBB, MBBI, TargetOpcode::COPY, destReg)
          .addReg(operandOne);
    }
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::A, RegState::Define)
        .addReg(destReg);
    buildMI(MBB, MBBI, I8085::SUI)
        .addImm(static_cast<uint8_t>(Op2.getImm()));
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(destReg, RegState::Define)
        .addReg(I8085::A);
  } else if (Op2IsA) {
    // Special case: Op2 is A register.
    // We want to compute: dest = operandOne - A
    // If we do the naive MOV A, operandOne; SUB A we get A - A = 0.
    // Instead, compute A - operandOne and negate: -(A - operandOne) = operandOne - A
    if (destReg == I8085::A) {
      // dest == A: A = operandOne - A
      // Compute A - operandOne, then negate
      buildMI(MBB, MBBI, I8085::SUB)
          .addReg(operandOne);
      // Negate A: CMA + INR A gives -A (two's complement)
      buildMI(MBB, MBBI, I8085::CMA);
      buildMI(MBB, MBBI, I8085::INR)
          .addReg(I8085::A, RegState::Define)
          .addReg(I8085::A);
    } else if (destReg == operandOne) {
      // dest == operandOne != A
      // dest = dest - A
      // Compute A - dest, then negate and store
      buildMI(MBB, MBBI, I8085::SUB)
          .addReg(destReg);
      buildMI(MBB, MBBI, I8085::CMA);
      buildMI(MBB, MBBI, I8085::INR)
          .addReg(I8085::A, RegState::Define)
          .addReg(I8085::A);
      buildMI(MBB, MBBI, I8085::MOV)
          .addReg(destReg, RegState::Define)
          .addReg(I8085::A);
    } else {
      // dest != operandOne, dest != A
      // Use dest as temporary to save A
      buildMI(MBB, MBBI, I8085::MOV)
          .addReg(destReg, RegState::Define)
          .addReg(I8085::A);
      buildMI(MBB, MBBI, I8085::MOV)
          .addReg(I8085::A, RegState::Define)
          .addReg(operandOne);
      buildMI(MBB, MBBI, I8085::SUB)
          .addReg(destReg);
      buildMI(MBB, MBBI, I8085::MOV)
          .addReg(destReg, RegState::Define)
          .addReg(I8085::A);
    }
  } else {
    // Normal case: Op2 is a register other than A
    if (destReg != operandOne) {
      buildMI(MBB, MBBI, TargetOpcode::COPY, destReg)
          .addReg(operandOne);
    }
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::A, RegState::Define)
        .addReg(destReg);
    buildMI(MBB, MBBI, I8085::SUB)
        .addReg(Op2.getReg());
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(destReg, RegState::Define)
        .addReg(I8085::A);
  }

  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::SUBI_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operandOne = MI.getOperand(1).getReg();
  int64_t Imm = MI.getOperand(2).getImm();

  if (destReg != operandOne) {
    buildMI(MBB, MBBI, TargetOpcode::COPY, destReg)
        .addReg(operandOne);
  }

  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(I8085::A, RegState::Define)
      .addReg(destReg);
  buildMI(MBB, MBBI, I8085::SUI)
      .addImm(static_cast<uint8_t>(Imm));
  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(destReg, RegState::Define)
      .addReg(I8085::A);

  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::INC16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  unsigned rd = MI.getOperand(0).getReg(); // tied to $src, holds the value
  buildMI(MBB, MBBI, I8085::INX)
      .addReg(rd, RegState::Define)
      .addReg(rd, RegState::Implicit);
  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::DEC16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  unsigned rd = MI.getOperand(0).getReg();
  buildMI(MBB, MBBI, I8085::DCX)
      .addReg(rd, RegState::Define)
      .addReg(rd, RegState::Implicit);
  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::ADD_16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operandOne = MI.getOperand(1).getReg();
  unsigned operandTwo = MI.getOperand(2).getReg();
  bool DstIsDead = MI.getOperand(0).isDead();
  unsigned workReg = destReg;
  bool copyBack = false;

  // If the destination pair is HL, DAD adds the other operand to HL in a single
  // byte (HL += rp). $src is tied to $dst so the first operand is already in HL.
  // DAD writes only CY (not Z/S/P/AC), so restrict it to where the full flags
  // are dead afterwards.
  if (destReg == I8085::HL) {
    unsigned addend = (operandOne == I8085::HL) ? operandTwo : operandOne;
    if (!isPhysRegLive(MBB, std::next(MBBI), I8085::SREG)) {
      buildMI(MBB, MBBI, I8085::DAD).addReg(addend);
      MI.eraseFromParent();
      return true;
    }
  }

  if (destReg == operandTwo && destReg != operandOne) {
    std::swap(operandOne, operandTwo);
  }

  unsigned opLow,opHigh;
  unsigned destLow,destHigh;

  if (!getPairRegs(workReg, destLow, destHigh))
    return false;
  if (workReg != operandOne) {
    buildMI(MBB, MBBI, TargetOpcode::COPY, workReg)
        .addReg(operandOne);
  }
  if (!getPairRegs(operandTwo, opLow, opHigh))
    return false;

  bool WorkIsDead = DstIsDead && !copyBack;
  buildMI(MBB, MBBI, I8085::ADD_8)
      .addReg(destLow, RegState::Define | getDeadRegState(WorkIsDead))
      .addReg(destLow)
      .addReg(opLow);
  
  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(I8085::A, RegState::Define)
      .addReg(destHigh);

  buildMI(MBB, MBBI, I8085::ADC)
      .addReg(opHigh);

  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(destHigh, RegState::Define | getDeadRegState(WorkIsDead))
      .addReg(I8085::A);

  if (copyBack) {
    buildMI(MBB, MBBI, TargetOpcode::COPY, destReg)
        .addReg(workReg);
  }

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::SUB_16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operandOne = MI.getOperand(1).getReg();
  unsigned operandTwo = MI.getOperand(2).getReg();  
  bool DstIsDead=MI.getOperand(0).isDead();
  unsigned workReg = destReg;
  bool copyBack = false;
  unsigned opLow,opHigh;
  unsigned destLow,destHigh;

  // Undoc fast path: DSUB = HL - BC (1 byte vs 6 bytes)
  if (HasUndoc && destReg == I8085::HL && operandTwo == I8085::BC) {
    if (operandOne != I8085::HL)
      buildMI(MBB, MBBI, TargetOpcode::COPY, I8085::HL).addReg(operandOne);
    buildMI(MBB, MBBI, I8085::DSUB);
    MI.eraseFromParent();
    return true;
  }

  if (destReg == operandTwo && destReg != operandOne) {
    if (!getPairRegs(destReg, destLow, destHigh))
      return false;
    if (!getPairRegs(operandOne, opLow, opHigh))
      return false;

    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::A, RegState::Define)
        .addReg(opLow);
    buildMI(MBB, MBBI, I8085::SUB)
        .addReg(destLow);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(destLow, RegState::Define | getDeadRegState(DstIsDead))
        .addReg(I8085::A);

    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::A, RegState::Define)
        .addReg(opHigh);
    buildMI(MBB, MBBI, I8085::SBB)
        .addReg(destHigh);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(destHigh, RegState::Define | getDeadRegState(DstIsDead))
        .addReg(I8085::A);

    MI.eraseFromParent();
    return true;
  }

  if (!getPairRegs(workReg, destLow, destHigh))
    return false;
  if (workReg != operandOne) {
    buildMI(MBB, MBBI, TargetOpcode::COPY, workReg)
        .addReg(operandOne);
  }
  if (!getPairRegs(operandTwo, opLow, opHigh))
    return false;
  bool WorkIsDead = DstIsDead && !copyBack;
  
  buildMI(MBB, MBBI, I8085::SUB_8)
      .addReg(destLow, RegState::Define | getDeadRegState(WorkIsDead))
      .addReg(destLow)
      .addReg(opLow);
  
  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(I8085::A, RegState::Define)
      .addReg(destHigh);

  buildMI(MBB, MBBI, I8085::SBB)
      .addReg(opHigh);

  buildMI(MBB, MBBI, I8085::MOV)
      .addReg(destHigh, RegState::Define | getDeadRegState(WorkIsDead))
      .addReg(I8085::A);

  if (copyBack) {
    buildMI(MBB, MBBI, TargetOpcode::COPY, destReg)
        .addReg(workReg);
  }

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::SET_EQ_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  
  unsigned operandOne = MI.getOperand(0).getReg();
  unsigned destReg = operandOne;
  uint16_t operandTwo = MI.getOperand(2).getReg();  

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A,RegState::Define)
    .addReg(destReg);

  buildMI(MBB, MBBI, I8085::SUB)
    .addReg(operandTwo);

  buildMI(MBB, MBBI, I8085::CMA);  

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(destReg,RegState::Define)
    .addReg(I8085::A);
  
  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::SET_NE_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  
  unsigned operandOne = MI.getOperand(0).getReg();
  unsigned destReg = operandOne;
  uint16_t operandTwo = MI.getOperand(2).getReg();  

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A,RegState::Define)
    .addReg(destReg);

  buildMI(MBB, MBBI, I8085::SUB)
    .addReg(operandTwo);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(destReg,RegState::Define)
    .addReg(I8085::A);
  
  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::SET_EQ_16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  
  unsigned operandOne = MI.getOperand(1).getReg();
  unsigned destReg = operandOne;
  uint16_t operandTwo = MI.getOperand(2).getReg();  
  bool DstIsDead=MI.getOperand(0).isDead();

  buildMI(MBB, MBBI, I8085::SUB_16)
    .addReg(destReg, RegState::Define | getDeadRegState(DstIsDead))
    .addReg(operandOne)
    .addReg(operandTwo);

  buildMI(MBB, MBBI, I8085::CMA);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(destReg,RegState::Define)
    .addReg(I8085::A);
  
  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::SET_NE_16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  
  unsigned operandOne = MI.getOperand(1).getReg();
  unsigned destReg = operandOne;
  uint16_t operandTwo = MI.getOperand(2).getReg();  
  bool DstIsDead=MI.getOperand(0).isDead();

  buildMI(MBB, MBBI, I8085::SUB_16)
    .addReg(destReg, RegState::Define | getDeadRegState(DstIsDead))
    .addReg(operandOne)
    .addReg(operandTwo);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(destReg,RegState::Define)
    .addReg(I8085::A);
  
  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::JMP_8_IF>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  
  unsigned operand = MI.getOperand(0).getReg();


  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A,RegState::Define)
    .addReg(operand);

  buildMI(MBB, MBBI, I8085::ORI)
    .addImm(0);

  buildMI(MBB, MBBI, I8085::JNZ).add(MI.getOperand(1));

  MI.eraseFromParent();
  return true;
}

// Fused compare-immediate-and-branch expansions.
// Each emits: MOV A, LHS; CPI imm; Jcc target
// Replaces the SET_*_8 diamond + JMP_8_IF pattern (saves ~15 bytes each).

static bool expandBrCCImm(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator MBBI,
                          unsigned JmpOpc,
                          const TargetInstrInfo *TII) {
  MachineInstr &MI = *MBBI;
  unsigned LHS = MI.getOperand(0).getReg();
  int64_t Imm = MI.getOperand(1).getImm();
  DebugLoc DL = MI.getDebugLoc();

  BuildMI(MBB, MBBI, DL, TII->get(I8085::MOV))
    .addReg(I8085::A, RegState::Define)
    .addReg(LHS);
  BuildMI(MBB, MBBI, DL, TII->get(I8085::CPI))
    .addImm(Imm);
  BuildMI(MBB, MBBI, DL, TII->get(JmpOpc))
    .add(MI.getOperand(2));

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::BR_CC_EQ_8_IMM>(Block &MBB, BlockIt MBBI) {
  return expandBrCCImm(MBB, MBBI, I8085::JZ, TII);
}

template <> bool I8085ExpandPseudo::expand<I8085::BR_CC_NE_8_IMM>(Block &MBB, BlockIt MBBI) {
  return expandBrCCImm(MBB, MBBI, I8085::JNZ, TII);
}

template <> bool I8085ExpandPseudo::expand<I8085::BR_CC_ULT_8_IMM>(Block &MBB, BlockIt MBBI) {
  return expandBrCCImm(MBB, MBBI, I8085::JC, TII);
}

template <> bool I8085ExpandPseudo::expand<I8085::BR_CC_UGE_8_IMM>(Block &MBB, BlockIt MBBI) {
  return expandBrCCImm(MBB, MBBI, I8085::JNC, TII);
}

// Signed compare-against-constant. The immediate operand is already biased
// (k ^ 0x80); emit MOV A,LHS; XRI 0x80; CPI biasedImm; Jcc. After XRI the
// accumulator holds LHS^0x80, so CPI sets CY = (LHS^0x80 < k^0x80) unsigned
// = signed(LHS) < signed(k).
static bool expandBrCCSignedImm(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MBBI,
                                unsigned JmpOpc,
                                const TargetInstrInfo *TII) {
  MachineInstr &MI = *MBBI;
  unsigned LHS = MI.getOperand(0).getReg();
  int64_t Imm = MI.getOperand(1).getImm();
  DebugLoc DL = MI.getDebugLoc();

  BuildMI(MBB, MBBI, DL, TII->get(I8085::MOV))
    .addReg(I8085::A, RegState::Define)
    .addReg(LHS);
  BuildMI(MBB, MBBI, DL, TII->get(I8085::XRI)).addImm(0x80);
  BuildMI(MBB, MBBI, DL, TII->get(I8085::CPI)).addImm(Imm);
  BuildMI(MBB, MBBI, DL, TII->get(JmpOpc)).add(MI.getOperand(2));

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::BR_CC_SLT_8_IMM>(Block &MBB, BlockIt MBBI) {
  return expandBrCCSignedImm(MBB, MBBI, I8085::JC, TII);
}

template <> bool I8085ExpandPseudo::expand<I8085::BR_CC_SGE_8_IMM>(Block &MBB, BlockIt MBBI) {
  return expandBrCCSignedImm(MBB, MBBI, I8085::JNC, TII);
}

// Sign test against 0: MOV A,LHS ; ORA A ; JM (a<0) / JP (a>=0).
static bool expandBrCCSignTest(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MBBI,
                               unsigned JmpOpc,
                               const TargetInstrInfo *TII) {
  MachineInstr &MI = *MBBI;
  unsigned LHS = MI.getOperand(0).getReg();
  DebugLoc DL = MI.getDebugLoc();

  BuildMI(MBB, MBBI, DL, TII->get(I8085::MOV))
    .addReg(I8085::A, RegState::Define)
    .addReg(LHS);
  BuildMI(MBB, MBBI, DL, TII->get(I8085::ORA)).addReg(I8085::A);
  BuildMI(MBB, MBBI, DL, TII->get(JmpOpc)).add(MI.getOperand(1));

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::BR_CC_MINUS_8>(Block &MBB, BlockIt MBBI) {
  return expandBrCCSignTest(MBB, MBBI, I8085::JM, TII);
}

template <> bool I8085ExpandPseudo::expand<I8085::BR_CC_PLUS_8>(Block &MBB, BlockIt MBBI) {
  return expandBrCCSignTest(MBB, MBBI, I8085::JP, TII);
}

// Fused compare-register-and-branch expansions.
// Each emits: MOV A, LHS ; SUB RHS ; Jcc target. After MOV A,LHS the accumulator
// holds LHS, and SUB RHS sets Z = (LHS == RHS) and CY = (LHS < RHS unsigned).
static bool expandBrCCReg(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator MBBI,
                          unsigned JmpOpc,
                          const TargetInstrInfo *TII) {
  MachineInstr &MI = *MBBI;
  unsigned LHS = MI.getOperand(0).getReg();
  unsigned RHS = MI.getOperand(1).getReg();
  DebugLoc DL = MI.getDebugLoc();

  BuildMI(MBB, MBBI, DL, TII->get(I8085::MOV))
    .addReg(I8085::A, RegState::Define)
    .addReg(LHS);
  BuildMI(MBB, MBBI, DL, TII->get(I8085::SUB))
    .addReg(RHS);
  BuildMI(MBB, MBBI, DL, TII->get(JmpOpc))
    .add(MI.getOperand(2));

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::BR_CC_EQ_8>(Block &MBB, BlockIt MBBI) {
  return expandBrCCReg(MBB, MBBI, I8085::JZ, TII);
}

template <> bool I8085ExpandPseudo::expand<I8085::BR_CC_NE_8>(Block &MBB, BlockIt MBBI) {
  return expandBrCCReg(MBB, MBBI, I8085::JNZ, TII);
}

template <> bool I8085ExpandPseudo::expand<I8085::BR_CC_ULT_8>(Block &MBB, BlockIt MBBI) {
  return expandBrCCReg(MBB, MBBI, I8085::JC, TII);
}

template <> bool I8085ExpandPseudo::expand<I8085::BR_CC_UGE_8>(Block &MBB, BlockIt MBBI) {
  return expandBrCCReg(MBB, MBBI, I8085::JNC, TII);
}

template <> bool I8085ExpandPseudo::expand<I8085::TRUNC16TO8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operand = MI.getOperand(1).getReg();

  unsigned operandLow,operandHigh;
  if (!getPairRegs(operand, operandLow, operandHigh))
    return false;
  
  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(destReg,RegState::Define)
    .addReg(operandLow);
     
  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::SEXT8TO16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operand = MI.getOperand(1).getReg();

  unsigned destLow,destHigh;
  if (!getPairRegs(destReg, destLow, destHigh))
    return false;
  
  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A,RegState::Define)
    .addReg(operand);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(destLow,RegState::Define)
    .addReg(I8085::A);

  buildMI(MBB, MBBI, I8085::ADI)
    .addImm(128); // 80h in hex .. [Adding 80h will set carry flag if HSB is high]


  buildMI(MBB, MBBI, I8085::SBB)
    .addReg(I8085::A);  // will result in FFh if CF set, 0 else

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(destHigh,RegState::Define)
    .addReg(I8085::A);  
     
  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::ZEXT8TO16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operand = MI.getOperand(1).getReg();

  unsigned destLow,destHigh;
  if (!getPairRegs(destReg, destLow, destHigh))
    return false;
  
  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A,RegState::Define)
    .addReg(operand);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(destLow,RegState::Define)
    .addReg(I8085::A);
  
  buildMI(MBB, MBBI, I8085::MVI)
    .addReg(destHigh,RegState::Define)
    .addImm(0);
     
  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::AEXT8TO16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  return expand<I8085::ZEXT8TO16>(MBB, MI);
}

template <> bool I8085ExpandPseudo::expand<I8085::BSWAP16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  unsigned srcLow, srcHigh;
  if (!getPairRegs(srcReg, srcLow, srcHigh))
    return false;

  unsigned destLow, destHigh;
  if (!getPairRegs(destReg, destLow, destHigh))
    return false;

  // BSWAP16: swap the two bytes
  // Use A as temporary to swap: A = srcLow, destLow = srcHigh, destHigh = A
  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A, RegState::Define)
    .addReg(srcLow);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(destLow, RegState::Define)
    .addReg(srcHigh);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(destHigh, RegState::Define)
    .addReg(I8085::A);

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::RL_16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  unsigned regLow, regHigh;

  // Undoc fast path: RDEL (1 byte vs ~12 bytes)
  if (HasUndoc && destReg == I8085::DE) {
    if (srcReg != I8085::DE)
      buildMI(MBB, MBBI, TargetOpcode::COPY, I8085::DE).addReg(srcReg);
    buildMI(MBB, MBBI, I8085::RDEL);
    MI.eraseFromParent();
    return true;
  }

  if (!getPairRegs(destReg, regLow, regHigh))
    return false;
  if (destReg != srcReg) {
    unsigned srcLow, srcHigh;
    if (!getPairRegs(srcReg, srcLow, srcHigh))
      return false;
    buildMI(MBB, MBBI, I8085::MOV)
      .addReg(regLow, RegState::Define)
      .addReg(srcLow);
    buildMI(MBB, MBBI, I8085::MOV)
      .addReg(regHigh, RegState::Define)
      .addReg(srcHigh);
  }

  // The first RAL consumes carry (RAL Uses SREG), so carry must be defined
  // here. STC alone suffices: whatever bit it shifts into regLow's bit0 is
  // discarded by the trailing ANI 254. The old STC;CMC (clear to 0) wasted an
  // extra CMC to force that masked-away bit to 0.
  buildMI(MBB, MBBI, I8085::STC);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A,RegState::Define)
    .addReg(regLow);

  buildMI(MBB, MBBI, I8085::RAL);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(regLow,RegState::Define)
    .addReg(I8085::A);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A,RegState::Define)
    .addReg(regHigh);

  buildMI(MBB, MBBI, I8085::RAL);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(regHigh,RegState::Define)
    .addReg(I8085::A);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A,RegState::Define)
    .addReg(regLow);

  buildMI(MBB, MBBI, I8085::ANI)
    .addImm(254);  

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(regLow,RegState::Define)
    .addReg(I8085::A);

  MI.eraseFromParent();
  return true;
}


template <> bool I8085ExpandPseudo::expand<I8085::RR_16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  unsigned regLow, regHigh;
  if (!getPairRegs(destReg, regLow, regHigh))
    return false;
  if (destReg != srcReg) {
    unsigned srcLow, srcHigh;
    if (!getPairRegs(srcReg, srcLow, srcHigh))
      return false;
    buildMI(MBB, MBBI, I8085::MOV)
      .addReg(regLow, RegState::Define)
      .addReg(srcLow);
    buildMI(MBB, MBBI, I8085::MOV)
      .addReg(regHigh, RegState::Define)
      .addReg(srcHigh);
  }

  // The first RAR consumes carry (RAR Uses SREG), so carry must be defined
  // here. STC alone suffices: whatever bit it shifts into regHigh's bit7 is
  // discarded by the trailing ANI 127. The old STC;CMC (clear to 0) wasted an
  // extra CMC to force that masked-away bit to 0.
  buildMI(MBB, MBBI, I8085::STC);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A,RegState::Define)
    .addReg(regHigh);

  buildMI(MBB, MBBI, I8085::RAR);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(regHigh,RegState::Define)
    .addReg(I8085::A);


  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A,RegState::Define)
    .addReg(regLow);

  buildMI(MBB, MBBI, I8085::RAR);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(regLow,RegState::Define)
    .addReg(I8085::A);


  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A,RegState::Define)
    .addReg(regHigh);

  buildMI(MBB, MBBI, I8085::ANI)
    .addImm(127);  

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(regHigh,RegState::Define)
    .addReg(I8085::A);

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::ASR_16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  unsigned regLow, regHigh;

  // Undoc fast path: ARHL (1 byte vs ~8 bytes)
  if (HasUndoc && destReg == I8085::HL) {
    if (srcReg != I8085::HL)
      buildMI(MBB, MBBI, TargetOpcode::COPY, I8085::HL).addReg(srcReg);
    buildMI(MBB, MBBI, I8085::ARHL);
    MI.eraseFromParent();
    return true;
  }

  if (!getPairRegs(destReg, regLow, regHigh))
    return false;
  if (destReg != srcReg) {
    unsigned srcLow, srcHigh;
    if (!getPairRegs(srcReg, srcLow, srcHigh))
      return false;
    buildMI(MBB, MBBI, I8085::MOV)
      .addReg(regLow, RegState::Define)
      .addReg(srcLow);
    buildMI(MBB, MBBI, I8085::MOV)
      .addReg(regHigh, RegState::Define)
      .addReg(srcHigh);
  }

  // Set carry from sign bit of the high byte.
  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A, RegState::Define)
    .addReg(regHigh);

  buildMI(MBB, MBBI, I8085::RLC);

  // Restore high byte and shift through carry to preserve sign.
  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A, RegState::Define)
    .addReg(regHigh);

  buildMI(MBB, MBBI, I8085::RAR);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(regHigh, RegState::Define)
    .addReg(I8085::A);

  // Shift low byte through carry.
  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A, RegState::Define)
    .addReg(regLow);

  buildMI(MBB, MBBI, I8085::RAR);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(regLow, RegState::Define)
    .addReg(I8085::A);

  MI.eraseFromParent();
  return true;
}

// srl i16, 8: [lo,hi] -> [hi, 0]
template <> bool I8085ExpandPseudo::expand<I8085::SRL_16_BY_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  unsigned regLow, regHigh;
  if (!getPairRegs(destReg, regLow, regHigh))
    return false;

  unsigned srcHigh;
  if (destReg == srcReg) {
    srcHigh = regHigh;
  } else {
    unsigned srcLow;
    if (!getPairRegs(srcReg, srcLow, srcHigh))
      return false;
  }

  // Move high byte to low position
  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(regLow, RegState::Define)
    .addReg(srcHigh);

  // Clear high byte
  buildMI(MBB, MBBI, I8085::MVI)
    .addReg(regHigh, RegState::Define)
    .addImm(0);

  MI.eraseFromParent();
  return true;
}

// shl i16, 8: [lo,hi] -> [0, lo]
template <> bool I8085ExpandPseudo::expand<I8085::SHL_16_BY_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  unsigned regLow, regHigh;
  if (!getPairRegs(destReg, regLow, regHigh))
    return false;

  unsigned srcLow;
  if (destReg == srcReg) {
    srcLow = regLow;
  } else {
    unsigned srcHigh;
    if (!getPairRegs(srcReg, srcLow, srcHigh))
      return false;
  }

  // Move low byte to high position (read before write to avoid clobber)
  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(regHigh, RegState::Define)
    .addReg(srcLow);

  // Clear low byte
  buildMI(MBB, MBBI, I8085::MVI)
    .addReg(regLow, RegState::Define)
    .addImm(0);

  MI.eraseFromParent();
  return true;
}

// ashr i16, 8: [lo,hi] -> [hi, sign_extend(hi.bit7)]
template <> bool I8085ExpandPseudo::expand<I8085::ASR_16_BY_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  unsigned regLow, regHigh;
  if (!getPairRegs(destReg, regLow, regHigh))
    return false;

  unsigned srcHigh;
  if (destReg == srcReg) {
    srcHigh = regHigh;
  } else {
    unsigned srcLow;
    if (!getPairRegs(srcReg, srcLow, srcHigh))
      return false;
  }

  // Move high byte to low position
  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(regLow, RegState::Define)
    .addReg(srcHigh);

  // Compute sign extension: ADI 128 sets carry if bit7 was 1; SBB A gives 0xFF/-1 or 0x00/0
  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A, RegState::Define)
    .addReg(srcHigh);

  buildMI(MBB, MBBI, I8085::ADI)
    .addImm(128);

  buildMI(MBB, MBBI, I8085::SBB)
    .addReg(I8085::A);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(regHigh, RegState::Define)
    .addReg(I8085::A);

  MI.eraseFromParent();
  return true;
}

// ashr i16, 15: sign extension → 0x0000 or 0xFFFF
template <> bool I8085ExpandPseudo::expand<I8085::SIGN_EXTEND_16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  unsigned regLow, regHigh;
  if (!getPairRegs(destReg, regLow, regHigh))
    return false;

  unsigned srcHigh;
  if (destReg == srcReg) {
    srcHigh = regHigh;
  } else {
    unsigned srcLow;
    if (!getPairRegs(srcReg, srcLow, srcHigh))
      return false;
  }

  // MOV A, srcHigh  — get the byte containing the sign bit
  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A, RegState::Define)
    .addReg(srcHigh);

  // ADI 128  — sets carry if bit7 was 1 (negative)
  buildMI(MBB, MBBI, I8085::ADI)
    .addImm(128);

  // SBB A  — A = 0xFF if carry (negative), 0x00 if no carry (positive)
  buildMI(MBB, MBBI, I8085::SBB)
    .addReg(I8085::A);

  // MOV regLow, A
  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(regLow, RegState::Define)
    .addReg(I8085::A);

  // MOV regHigh, A
  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(regHigh, RegState::Define)
    .addReg(I8085::A);

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::RL_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();
  if (destReg != srcReg) {
    buildMI(MBB, MBBI, I8085::MOV)
      .addReg(destReg, RegState::Define)
      .addReg(srcReg);
  }

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A,RegState::Define)
    .addReg(destReg);

  buildMI(MBB, MBBI, I8085::ADD)
    .addReg(I8085::A);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(destReg,RegState::Define)
    .addReg(I8085::A);

  MI.eraseFromParent();
  return true;
}


template <> bool I8085ExpandPseudo::expand<I8085::RR_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();
  if (destReg != srcReg) {
    buildMI(MBB, MBBI, I8085::MOV)
      .addReg(destReg, RegState::Define)
      .addReg(srcReg);
  }

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A,RegState::Define)
    .addReg(destReg);

  buildMI(MBB, MBBI, I8085::RRC);

  buildMI(MBB, MBBI, I8085::ANI)
    .addImm(127);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(destReg,RegState::Define)
    .addReg(I8085::A);

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::ASR_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();
  if (destReg != srcReg) {
    buildMI(MBB, MBBI, I8085::MOV)
      .addReg(destReg, RegState::Define)
      .addReg(srcReg);
  }

  // Set carry from sign bit.
  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A, RegState::Define)
    .addReg(destReg);

  buildMI(MBB, MBBI, I8085::RLC);

  // Restore and shift through carry.
  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A, RegState::Define)
    .addReg(destReg);

  buildMI(MBB, MBBI, I8085::RAR);

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(destReg, RegState::Define)
    .addReg(I8085::A);

  MI.eraseFromParent();
  return true;
}

// In-place ++ of a byte at a fixed address: LXI H, addr ; INR M.
template <>
bool I8085ExpandPseudo::expand<I8085::INC_MEM_8_WITH_IMM_ADDR>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  const MachineOperand &AddrMO = MI.getOperand(0);
  MachineInstrBuilder Addr =
      buildMI(MBB, MBBI, I8085::LXI).addReg(I8085::HL, RegState::Define);
  addAddrOperand(Addr, AddrMO);
  buildMI(MBB, MBBI, I8085::INR_M);
  MI.eraseFromParent();
  return true;
}

// In-place -- of a byte at a fixed address: LXI H, addr ; DCR M.
template <>
bool I8085ExpandPseudo::expand<I8085::DEC_MEM_8_WITH_IMM_ADDR>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  const MachineOperand &AddrMO = MI.getOperand(0);
  MachineInstrBuilder Addr =
      buildMI(MBB, MBBI, I8085::LXI).addReg(I8085::HL, RegState::Define);
  addAddrOperand(Addr, AddrMO);
  buildMI(MBB, MBBI, I8085::DCR_M);
  MI.eraseFromParent();
  return true;
}

// In-place ++/-- of a byte at an address held in a GR16 register. Move the
// pointer into HL (INR M / DCR M only address via HL), then INR M / DCR M.
// incDecOpc selects INR_M or DCR_M.
bool I8085ExpandPseudo::expandMemIncDecContent(Block &MBB, BlockIt MBBI,
                                               unsigned incDecOpc) {
  MachineInstr &MI = *MBBI;
  unsigned addrReg = MI.getOperand(0).getReg();
  if (addrReg == I8085::SP) {
    buildMI(MBB, MBBI, I8085::LXI).addReg(I8085::HL, RegState::Define).addImm(0);
    buildMI(MBB, MBBI, I8085::DAD).addReg(I8085::SP);
  } else if (addrReg != I8085::HL) {
    unsigned lowReg, highReg;
    if (!getPairRegs(addrReg, lowReg, highReg))
      return false;
    buildMI(MBB, MBBI, I8085::MOV).addReg(I8085::H, RegState::Define).addReg(highReg);
    buildMI(MBB, MBBI, I8085::MOV).addReg(I8085::L, RegState::Define).addReg(lowReg);
  }
  buildMI(MBB, MBBI, incDecOpc);
  MI.eraseFromParent();
  return true;
}

template <>
bool I8085ExpandPseudo::expand<I8085::INC_MEM_8_ADDR_CONTENT>(Block &MBB, BlockIt MBBI) {
  return expandMemIncDecContent(MBB, MBBI, I8085::INR_M);
}

template <>
bool I8085ExpandPseudo::expand<I8085::DEC_MEM_8_ADDR_CONTENT>(Block &MBB, BlockIt MBBI) {
  return expandMemIncDecContent(MBB, MBBI, I8085::DCR_M);
}

// shl i8 by n (5..7) == circular rotate right by (8-n), then clear the low n
// bits that wrapped in. Rotating the short way is 8-n RRCs (<=3) instead of a
// runtime loop.
template <>
bool I8085ExpandPseudo::expand<I8085::SHL_8_HI>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();
  unsigned n = MI.getOperand(2).getImm();
  buildMI(MBB, MBBI, I8085::MOV).addReg(I8085::A, RegState::Define).addReg(srcReg);
  for (unsigned i = 0; i < 8 - n; ++i)
    buildMI(MBB, MBBI, I8085::RRC);
  buildMI(MBB, MBBI, I8085::ANI).addImm((0xFF << n) & 0xFF);
  buildMI(MBB, MBBI, I8085::MOV).addReg(destReg, RegState::Define).addReg(I8085::A);
  MI.eraseFromParent();
  return true;
}

// lshr i8 by n (5..7) == circular rotate left by (8-n), then clear the high n
// bits that wrapped in.
template <>
bool I8085ExpandPseudo::expand<I8085::SRL_8_HI>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();
  unsigned n = MI.getOperand(2).getImm();
  buildMI(MBB, MBBI, I8085::MOV).addReg(I8085::A, RegState::Define).addReg(srcReg);
  for (unsigned i = 0; i < 8 - n; ++i)
    buildMI(MBB, MBBI, I8085::RLC);
  buildMI(MBB, MBBI, I8085::ANI).addImm(0xFF >> n);
  buildMI(MBB, MBBI, I8085::MOV).addReg(destReg, RegState::Define).addReg(I8085::A);
  MI.eraseFromParent();
  return true;
}

// ashr i8 by 7: smear the sign bit. ADD A doubles A so CY = old bit7; SBB A then
// computes A - A - CY = -CY = 0x00 (sign 0) or 0xFF (sign 1).
template <>
bool I8085ExpandPseudo::expand<I8085::ASR_8_BY_7>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();
  buildMI(MBB, MBBI, I8085::MOV).addReg(I8085::A, RegState::Define).addReg(srcReg);
  buildMI(MBB, MBBI, I8085::ADD).addReg(I8085::A);
  buildMI(MBB, MBBI, I8085::SBB).addReg(I8085::A);
  buildMI(MBB, MBBI, I8085::MOV).addReg(destReg, RegState::Define).addReg(I8085::A);
  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::JMP_16_IF>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  
  unsigned operandOne = MI.getOperand(0).getReg();

  unsigned opOneLow,opOneHigh;
  if (!getPairRegs(operandOne, opOneLow, opOneHigh))
    return false;

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A,RegState::Define)
    .addReg(opOneHigh);

  buildMI(MBB, MBBI, I8085::ANI)
    .addImm(255);

  buildMI(MBB, MBBI, I8085::JNZ)
    .addMBB(MI.getOperand(1).getMBB());

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A,RegState::Define)
    .addReg(opOneLow);

  buildMI(MBB, MBBI, I8085::ANI)
    .addImm(255);

  buildMI(MBB, MBBI, I8085::JNZ)
    .addMBB(MI.getOperand(1).getMBB());
  
  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::JMP_16_IF_NOT_EQUAL>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  MachineFunction *MF = MBB.getParent();
  const DebugLoc &DL = MI.getDebugLoc();
  
  unsigned operandOne = MI.getOperand(0).getReg();
  unsigned operandTwo = MI.getOperand(1).getReg();
  MachineBasicBlock *TargetMBB = MI.getOperand(2).getMBB();

  unsigned opOneLow,opOneHigh;
  unsigned opTwoLow,opTwoHigh;
  if (!getPairRegs(operandOne, opOneLow, opOneHigh))
    return false;
  if (!getPairRegs(operandTwo, opTwoLow, opTwoHigh))
    return false;

  const BasicBlock *LLVMBB = MBB.getBasicBlock();
  MachineBasicBlock *CmpLowMBB = MF->CreateMachineBasicBlock(LLVMBB);
  MachineBasicBlock *TailMBB = MF->CreateMachineBasicBlock(LLVMBB);
  MachineBasicBlock *DiffMBB = MF->CreateMachineBasicBlock(LLVMBB);
  auto InsertPos = std::next(MBB.getIterator());
  MF->insert(InsertPos, CmpLowMBB);
  MF->insert(InsertPos, TailMBB);
  MF->insert(InsertPos, DiffMBB);
  // Renumber ALL blocks to avoid conflicts with existing block numbers.
  MF->RenumberBlocks();

  TailMBB->splice(TailMBB->begin(), &MBB, std::next(MBBI), MBB.end());
  TailMBB->transferSuccessorsAndUpdatePHIs(&MBB);
  if (TailMBB->isSuccessor(TargetMBB)) {
    TailMBB->removeSuccessor(TargetMBB);
    TargetMBB->replacePhiUsesWith(TailMBB, DiffMBB);
  }

  MBB.addSuccessor(CmpLowMBB);
  MBB.addSuccessor(DiffMBB);
  CmpLowMBB->addSuccessor(TailMBB);
  CmpLowMBB->addSuccessor(DiffMBB);
  DiffMBB->addSuccessor(TargetMBB);

  BuildMI(MBB, MBBI, DL, TII->get(I8085::MOV))
      .addReg(I8085::A, RegState::Define)
      .addReg(opOneHigh);
  BuildMI(MBB, MBBI, DL, TII->get(I8085::CMP)).addReg(opTwoHigh);
  BuildMI(MBB, MBBI, DL, TII->get(I8085::JNZ)).addMBB(DiffMBB);
  BuildMI(MBB, MBBI, DL, TII->get(I8085::JMP)).addMBB(CmpLowMBB);

  BuildMI(CmpLowMBB, DL, TII->get(I8085::MOV))
      .addReg(I8085::A, RegState::Define)
      .addReg(opOneLow);
  BuildMI(CmpLowMBB, DL, TII->get(I8085::CMP)).addReg(opTwoLow);
  BuildMI(CmpLowMBB, DL, TII->get(I8085::JNZ)).addMBB(DiffMBB);
  BuildMI(CmpLowMBB, DL, TII->get(I8085::JMP)).addMBB(TailMBB);

  BuildMI(DiffMBB, DL, TII->get(I8085::JMP)).addMBB(TargetMBB);

  if (TailMBB->succ_size() == 1) {
    auto Last = TailMBB->getLastNonDebugInstr();
    if (Last == TailMBB->end() || !Last->isTerminator())
      BuildMI(TailMBB, DL, TII->get(I8085::JMP))
          .addMBB(*TailMBB->succ_begin());
  }

  LivePhysRegs LiveRegs;
  computeAndAddLiveIns(LiveRegs, *TailMBB);
  computeAndAddLiveIns(LiveRegs, *DiffMBB);
  computeAndAddLiveIns(LiveRegs, *CmpLowMBB);

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::JMP_16_IF_ZERO>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned operand = MI.getOperand(0).getReg();

  unsigned opLow, opHigh;
  if (!getPairRegs(operand, opLow, opHigh))
    return false;

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A, RegState::Define)
    .addReg(opHigh);

  buildMI(MBB, MBBI, I8085::ORA)
    .addReg(opLow);

  buildMI(MBB, MBBI, I8085::JZ)
    .addMBB(MI.getOperand(1).getMBB());

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::JMP_16_IF_NOT_ZERO>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned operand = MI.getOperand(0).getReg();

  unsigned opLow, opHigh;
  if (!getPairRegs(operand, opLow, opHigh))
    return false;

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A, RegState::Define)
    .addReg(opHigh);

  buildMI(MBB, MBBI, I8085::ORA)
    .addReg(opLow);

  buildMI(MBB, MBBI, I8085::JNZ)
    .addMBB(MI.getOperand(1).getMBB());

  MI.eraseFromParent();
  return true;
}



template <> bool I8085ExpandPseudo::expand<I8085::JMP_16_IF_SAME_SIGN>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  
  unsigned operandOne = MI.getOperand(0).getReg();
  unsigned operandTwo = MI.getOperand(1).getReg();

  unsigned opOneLow,opOneHigh;
  unsigned opTwoLow,opTwoHigh;
  if (!getPairRegs(operandOne, opOneLow, opOneHigh))
    return false;
  if (!getPairRegs(operandTwo, opTwoLow, opTwoHigh))
    return false;

  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A,RegState::Define)
    .addReg(opOneHigh);

  buildMI(MBB, MBBI, I8085::XRA)
    .addReg(opTwoHigh);

  buildMI(MBB, MBBI, I8085::ANI)
        .addImm(128);  

  buildMI(MBB, MBBI, I8085::JZ)
    .addMBB(MI.getOperand(2).getMBB());
  
  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::JMP_16_IF_POSITIVE>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  
  unsigned operandOne = MI.getOperand(0).getReg();

  unsigned opOneLow,opOneHigh;
  if (!getPairRegs(operandOne, opOneLow, opOneHigh))
    return false;


  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::A,RegState::Define)
    .addReg(opOneHigh);

  buildMI(MBB, MBBI, I8085::ANI)
        .addImm(128);  

  buildMI(MBB, MBBI, I8085::JZ)
    .addMBB(MI.getOperand(1).getMBB());
  
  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::STORE_8_AT_OFFSET_WITH_SP>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned srcReg = MI.getOperand(0).getReg();
  int64_t offsetToStore = MI.getOperand(1).getImm();

  // Undoc fast path: LDSI + STAX D (3 bytes) vs LXI H+offset; DAD SP; MOV M,r (5+ bytes)
  // LDSI clobbers DE. Safe if: DE is dead, OR we're writing D/E (copied to A first).
  if (HasUndoc && offsetToStore >= 0 && offsetToStore <= 255) {
    bool DEUsed = isPhysRegLive(MBB, MBBI, I8085::DE) ||
                  isPhysRegLive(MBB, MBBI, I8085::D) ||
                  isPhysRegLive(MBB, MBBI, I8085::E);
    // If the store source IS D or E, we copy it to A before LDSI, so DE is safe.
    bool DEOk = !DEUsed || srcReg == I8085::D || srcReg == I8085::E;
    if (DEOk) {
      if (srcReg != I8085::A)
        buildMI(MBB, MBBI, I8085::MOV).addReg(I8085::A, RegState::Define).addReg(srcReg);
      buildMI(MBB, MBBI, I8085::LDSI).addImm(offsetToStore);
      buildMI(MBB, MBBI, I8085::STAX).addReg(I8085::DE);
      MI.eraseFromParent();
      return true;
    }
  }

  buildMI(MBB, MBBI, I8085::STORE_8)
    .addReg(I8085::SP)
    .addImm(offsetToStore)
    .addReg(srcReg);
  
  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::STORE_16_AT_OFFSET_WITH_SP>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned srcReg = MI.getOperand(0).getReg();
  int64_t offsetToStore = MI.getOperand(1).getImm();

  // Undoc fast path: LDSI + SHLX (3 bytes) vs LXI H+offset; DAD SP; MOV M,lo; INX H; MOV M,hi (7+ bytes)
  // LDSI clobbers DE. Safe if: DE is dead, OR srcReg IS DE (copied to HL first).
  if (HasUndoc && offsetToStore >= 0 && offsetToStore <= 255) {
    bool DEUsed = isPhysRegLive(MBB, MBBI, I8085::DE) ||
                  isPhysRegLive(MBB, MBBI, I8085::D) ||
                  isPhysRegLive(MBB, MBBI, I8085::E);
    bool DEOk = !DEUsed || srcReg == I8085::DE;
    if (DEOk) {
      if (srcReg != I8085::HL)
        buildMI(MBB, MBBI, TargetOpcode::COPY, I8085::HL).addReg(srcReg);
      buildMI(MBB, MBBI, I8085::LDSI).addImm(offsetToStore);
      buildMI(MBB, MBBI, I8085::SHLX);
      MI.eraseFromParent();
      return true;
    }
  }

  buildMI(MBB, MBBI, I8085::STORE_16)
    .addReg(I8085::SP)
    .addImm(offsetToStore)
    .addReg(srcReg);
  
  MI.eraseFromParent();
  return true;
}

/// Expand MUL_16_IMM: multiply a 16-bit register by a constant using
/// shift-add chains operating directly on HL with DAD instructions.
///
/// The expansion operates entirely in HL (accumulator) and BC (save register):
///   DAD H  = HL <<= 1  (1 byte, 10 cycles)
///   MOV B,H; MOV C,L   = save HL to BC  (2 bytes, 8 cycles)
///   DAD B  = HL += BC   (1 byte, 10 cycles)
///   SUB: MOV A,L; SUB C; MOV L,A; MOV A,H; SBB B; MOV H,A  (6 bytes, 24 cycles)
template <> bool I8085ExpandPseudo::expand<I8085::MUL_16_IMM>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();
  uint16_t CVal = MI.getOperand(2).getImm();

  // Helper lambdas for emitting instructions
  auto emitCopyToHL = [&]() {
    if (srcReg != I8085::HL)
      buildMI(MBB, MBBI, TargetOpcode::COPY, I8085::HL).addReg(srcReg);
  };

  auto emitDADH = [&]() {
    buildMI(MBB, MBBI, I8085::DAD).addReg(I8085::HL);
  };

  auto emitSaveToBC = [&]() {
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::B, RegState::Define).addReg(I8085::H);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::C, RegState::Define).addReg(I8085::L);
  };

  auto emitDADB = [&]() {
    buildMI(MBB, MBBI, I8085::DAD).addReg(I8085::BC);
  };

  auto emitSubBC = [&]() {
    // HL -= BC: MOV A,L; SUB C; MOV L,A; MOV A,H; SBB B; MOV H,A
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::A, RegState::Define).addReg(I8085::L);
    buildMI(MBB, MBBI, I8085::SUB).addReg(I8085::C);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::L, RegState::Define).addReg(I8085::A);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::A, RegState::Define).addReg(I8085::H);
    buildMI(MBB, MBBI, I8085::SBB).addReg(I8085::B);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::H, RegState::Define).addReg(I8085::A);
  };

  auto emitShiftN = [&](unsigned N) {
    for (unsigned i = 0; i < N; ++i)
      emitDADH();
  };

  auto emitCopyFromHL = [&]() {
    if (destReg != I8085::HL)
      buildMI(MBB, MBBI, TargetOpcode::COPY, destReg).addReg(I8085::HL);
  };

  // Determine the best decomposition strategy (mirrors ISel cost model)
  unsigned TZ = llvm::countr_zero(CVal);
  uint16_t Core = CVal >> TZ;

  struct BestDecomp {
    unsigned Strategy = 0;
    unsigned Bytes = UINT_MAX;
    unsigned Cycles = UINT_MAX;
    unsigned Param1 = 0, Param2 = 0;
  } Best;

  auto tryBetter = [&](unsigned Strategy, unsigned Bytes, unsigned Cycles,
                        unsigned P1 = 0, unsigned P2 = 0) {
    if (Bytes < Best.Bytes || (Bytes == Best.Bytes && Cycles < Best.Cycles)) {
      Best = {Strategy, Bytes, Cycles, P1, P2};
    }
  };

  // Strategy 1: (2^a + 1) * 2^tz
  if (Core >= 3 && ((Core - 1) & (Core - 2)) == 0) {
    unsigned A = llvm::countr_zero((uint16_t)(Core - 1));
    tryBetter(1, A + 3 + TZ, A * 10 + 18 + TZ * 10, A);
  }
  // Strategy 2: (2^a - 1) * 2^tz
  if (Core >= 3 && ((Core + 1) & Core) == 0) {
    unsigned A = llvm::countr_zero((uint16_t)(Core + 1));
    tryBetter(2, A + 8 + TZ, A * 10 + 32 + TZ * 10, A);
  }
  // Strategy 3: popcount 2
  if (llvm::popcount(CVal) == 2) {
    unsigned B = llvm::countr_zero(CVal);
    unsigned A = 15 - llvm::countl_zero(CVal);
    tryBetter(3, B + 2 + (A - B) + 1, B * 10 + 8 + (A - B) * 10 + 10, A, B);
  }
  // Strategy 4: run of 1s
  {
    uint16_t Lo = CVal & (-CVal);
    uint16_t Sum = CVal + Lo;
    if (Sum && (Sum & (Sum - 1)) == 0) {
      unsigned A = llvm::countr_zero(Sum);
      unsigned B = llvm::countr_zero(Lo);
      tryBetter(4, B + 2 + (A - B) + 6, B * 10 + 8 + (A - B) * 10 + 24, A, B);
    }
  }
  // Strategy 5/6: factored
  if (Core > 1) {
    for (unsigned A = 1; A <= 7; ++A) {
      uint16_t F1 = (1u << A) + 1;
      if (F1 > Core) break;
      if (Core % F1 != 0) continue;
      uint16_t F2 = Core / F1;
      if (F2 <= 1) continue;
      if (F2 >= 3 && ((F2 - 1) & (F2 - 2)) == 0) {
        unsigned B = llvm::countr_zero((uint16_t)(F2 - 1));
        tryBetter(5, A + 3 + B + 3 + TZ, A * 10 + 18 + B * 10 + 18 + TZ * 10, A, B);
      }
      if ((F2 & (F2 - 1)) == 0) {
        unsigned B = llvm::countr_zero(F2);
        tryBetter(6, A + 3 + B + TZ, A * 10 + 18 + B * 10 + TZ * 10, A, B);
      }
    }
  }
  // Strategy 71/72: C+1 decomp
  {
    uint16_t Cp = CVal + 1;
    if (Cp > 0) {
      if ((Cp & (Cp - 1)) == 0) {
        unsigned N = llvm::countr_zero(Cp);
        tryBetter(71, N + 8, N * 10 + 32, N);
      } else {
        unsigned TZp = llvm::countr_zero(Cp);
        uint16_t Corep = Cp >> TZp;
        if (Corep >= 3 && ((Corep - 1) & (Corep - 2)) == 0) {
          unsigned A = llvm::countr_zero((uint16_t)(Corep - 1));
          tryBetter(72, A + 3 + TZp + 8, A * 10 + 18 + TZp * 10 + 32, A, TZp);
        }
      }
    }
  }
  // Strategy 81/82: C-1 decomp
  {
    uint16_t Cm = CVal - 1;
    if (Cm > 1) {
      if ((Cm & (Cm - 1)) == 0) {
        unsigned N = llvm::countr_zero(Cm);
        tryBetter(81, N + 3, N * 10 + 18, N);
      } else {
        unsigned TZp = llvm::countr_zero(Cm);
        uint16_t Corep = Cm >> TZp;
        if (Corep >= 3 && ((Corep - 1) & (Corep - 2)) == 0) {
          unsigned A = llvm::countr_zero((uint16_t)(Corep - 1));
          tryBetter(82, A + 3 + TZp + 3, A * 10 + 18 + TZp * 10 + 18, A, TZp);
        }
      }
    }
  }

  // Emit the expansion based on the winning strategy.
  emitCopyToHL();

  switch (Best.Strategy) {
  case 1: {
    // (2^a + 1) * 2^tz
    unsigned A = Best.Param1;
    emitSaveToBC();
    emitShiftN(A);
    emitDADB();
    emitShiftN(TZ);
    break;
  }
  case 2: {
    // (2^a - 1) * 2^tz
    unsigned A = Best.Param1;
    emitSaveToBC();
    emitShiftN(A);
    emitSubBC();
    emitShiftN(TZ);
    break;
  }
  case 3: {
    // 2^a + 2^b
    unsigned A = Best.Param1, B = Best.Param2;
    emitShiftN(B);
    emitSaveToBC();
    emitShiftN(A - B);
    emitDADB();
    break;
  }
  case 4: {
    // 2^a - 2^b (run of 1s)
    unsigned A = Best.Param1, B = Best.Param2;
    emitShiftN(B);
    emitSaveToBC();
    emitShiftN(A - B);
    emitSubBC();
    break;
  }
  case 5: {
    // (2^a+1) * (2^b+1) * 2^tz
    unsigned A = Best.Param1, B = Best.Param2;
    emitSaveToBC();
    emitShiftN(A);
    emitDADB();
    emitSaveToBC();
    emitShiftN(B);
    emitDADB();
    emitShiftN(TZ);
    break;
  }
  case 6: {
    // (2^a+1) * 2^b * 2^tz
    unsigned A = Best.Param1, B = Best.Param2;
    emitSaveToBC();
    emitShiftN(A);
    emitDADB();
    emitShiftN(B);
    emitShiftN(TZ);
    break;
  }
  case 71: {
    // C = 2^n - 1
    unsigned N = Best.Param1;
    emitSaveToBC();
    emitShiftN(N);
    emitSubBC();
    break;
  }
  case 72: {
    // C+1 = (2^a+1)*2^tz: shift-add, shift, sub x
    unsigned A = Best.Param1, TZp = Best.Param2;
    emitSaveToBC();
    emitShiftN(A);
    emitDADB();
    emitShiftN(TZp);
    emitSubBC();
    break;
  }
  case 81: {
    // C = 2^n + 1
    unsigned N = Best.Param1;
    emitSaveToBC();
    emitShiftN(N);
    emitDADB();
    break;
  }
  case 82: {
    // C-1 = (2^a+1)*2^tz: shift-add, shift, add x
    unsigned A = Best.Param1, TZp = Best.Param2;
    emitSaveToBC();
    emitShiftN(A);
    emitDADB();
    emitShiftN(TZp);
    emitDADB();
    break;
  }
  default:
    llvm_unreachable("MUL_16_IMM: no decomposition strategy matched");
  }

  emitCopyFromHL();

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo::expand<I8085::RETI>(Block &MBB, BlockIt MBBI) {
  buildMI(MBB, MBBI, I8085::EI);
  buildMI(MBB, MBBI, I8085::RET);
  MBBI->eraseFromParent();
  return true;
}

bool I8085ExpandPseudo::expandMI(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  int Opcode = MBBI->getOpcode();

  if (Opcode == I8085::MOV && MI.getNumOperands() >= 2 &&
      MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
      MI.getOperand(0).getReg() == MI.getOperand(1).getReg()) {
    MI.eraseFromParent();
    return true;
  }

#define EXPAND(Op)                                                             \
  case Op:                                                                     \
    return expand<Op>(MBB, MI)

  switch (Opcode) {
    EXPAND(I8085::LOAD_16_ADDR_CONTENT);
    EXPAND(I8085::LOAD_8_ADDR_CONTENT);
    EXPAND(I8085::STORE_16_AT_OFFSET_WITH_SP);
    EXPAND(I8085::STORE_8_AT_OFFSET_WITH_SP);
    EXPAND(I8085::JMP_16_IF);
    EXPAND(I8085::JMP_16_IF_POSITIVE);
    EXPAND(I8085::JMP_16_IF_SAME_SIGN);
    EXPAND(I8085::JMP_16_IF_NOT_EQUAL);
    EXPAND(I8085::JMP_16_IF_ZERO);
    EXPAND(I8085::JMP_16_IF_NOT_ZERO);
    EXPAND(I8085::JMP_8_IF);
    EXPAND(I8085::BR_CC_EQ_8_IMM);
    EXPAND(I8085::BR_CC_NE_8_IMM);
    EXPAND(I8085::BR_CC_ULT_8_IMM);
    EXPAND(I8085::BR_CC_UGE_8_IMM);
    EXPAND(I8085::BR_CC_SLT_8_IMM);
    EXPAND(I8085::BR_CC_SGE_8_IMM);
    EXPAND(I8085::BR_CC_MINUS_8);
    EXPAND(I8085::BR_CC_PLUS_8);
    EXPAND(I8085::BR_CC_EQ_8);
    EXPAND(I8085::BR_CC_NE_8);
    EXPAND(I8085::BR_CC_ULT_8);
    EXPAND(I8085::BR_CC_UGE_8);
    EXPAND(I8085::TRUNC16TO8);
    EXPAND(I8085::AEXT8TO16);
    EXPAND(I8085::SEXT8TO16);
    EXPAND(I8085::ZEXT8TO16);
    EXPAND(I8085::BSWAP16);
    EXPAND(I8085::RL_16);
    EXPAND(I8085::RR_16);
    EXPAND(I8085::ASR_16);
    EXPAND(I8085::SHL_16_BY_8);
    EXPAND(I8085::SRL_16_BY_8);
    EXPAND(I8085::ASR_16_BY_8);
    EXPAND(I8085::SIGN_EXTEND_16);
    EXPAND(I8085::RL_8);
    EXPAND(I8085::RR_8);
    EXPAND(I8085::ASR_8);
    EXPAND(I8085::SHL_8_HI);
    EXPAND(I8085::SRL_8_HI);
    EXPAND(I8085::ASR_8_BY_7);
    EXPAND(I8085::NOT_16);
    EXPAND(I8085::INC_MEM_8_WITH_IMM_ADDR);
    EXPAND(I8085::DEC_MEM_8_WITH_IMM_ADDR);
    EXPAND(I8085::INC_MEM_8_ADDR_CONTENT);
    EXPAND(I8085::DEC_MEM_8_ADDR_CONTENT);
    EXPAND(I8085::STORE_16_ADDR_CONTENT);
    EXPAND(I8085::STORE_8_ADDR_CONTENT);
    EXPAND(I8085::CALL_INDIRECT);
    // EXPAND(I8085::SET_NE_16);
    // EXPAND(I8085::SET_EQ_16);
    // EXPAND(I8085::SET_NE_8);
    // EXPAND(I8085::SET_EQ_8);
    EXPAND(I8085::XOR_16);
    EXPAND(I8085::OR_16);
    EXPAND(I8085::AND_16);
    EXPAND(I8085::XOR_8);
    EXPAND(I8085::OR_8);
    EXPAND(I8085::AND_8);
    EXPAND(I8085::XORI_8);
    EXPAND(I8085::ORI_8);
    EXPAND(I8085::ANDI_8);
    EXPAND(I8085::SUB_16);
    EXPAND(I8085::ADD_16);
    EXPAND(I8085::INC16);
    EXPAND(I8085::DEC16);
    EXPAND(I8085::SUB_8);
    EXPAND(I8085::SUBI_8);
    EXPAND(I8085::ADD_8);
    EXPAND(I8085::LOAD_16_WITH_ADDR);
    EXPAND(I8085::LOAD_8_WITH_ADDR);
    EXPAND(I8085::LOAD_8_WITH_IMM_ADDR);
    EXPAND(I8085::LOAD_16_WITH_IMM_ADDR);
    EXPAND(I8085::LOAD_16);
    EXPAND(I8085::FRMIDX);
    EXPAND(I8085::STORE_8_WITH_IMM_ADDR);
    EXPAND(I8085::STORE_16_WITH_IMM_ADDR);
    EXPAND(I8085::STORE_16);
    EXPAND(I8085::SHRINK_STACK_BY);
    EXPAND(I8085::GROW_STACK_BY);
    EXPAND(I8085::STORE_8);
    EXPAND(I8085::MUL_16_IMM);
    EXPAND(I8085::RETI);
  }
#undef EXPAND
  return false;
}

} // end of anonymous namespace

INITIALIZE_PASS(I8085ExpandPseudo, "i8085-expand-pseudo", I8085_EXPAND_PSEUDO_NAME,
                false, false)
namespace llvm {

FunctionPass *createI8085ExpandPseudoPass() { return new I8085ExpandPseudo(); }

} // end of namespace llvm
