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
#include "I8085MachineFunctionInfo.h"
#include "I8085TargetMachine.h"
#include "MCTargetDesc/I8085MCTargetDesc.h"

#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/Support/Debug.h"
#include <stdint.h>

#include <array>
#include <iostream>


#define DEBUG_TYPE "i8085-expand-psuedo-32"


using namespace llvm;

#define I8085_EXPAND_PSEUDO_32_NAME "I8085 pseudo instruction expansion pass for instructions with 32 bit imaginary registers"

namespace {

/// Expands "placeholder" instructions which uses 32 bit imaginary registers marked as pseudo into
/// actual I8085 instructions.
class I8085ExpandPseudo32 : public MachineFunctionPass {
public:
  static char ID;

  I8085ExpandPseudo32() : MachineFunctionPass(ID) {
    initializeI8085ExpandPseudo32Pass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return I8085_EXPAND_PSEUDO_32_NAME; }

private:
  typedef MachineBasicBlock Block;
  typedef Block::iterator BlockIt;

  const I8085RegisterInfo *TRI;
  const TargetInstrInfo *TII;
  bool HasUndoc;
  int ScratchFI = -1;
  int64_t ScratchBaseOffset = 0;
  bool HaveScratch = false;
  bool IBXRemapped = false;
  // Track mid-function SP adjustments (GROW_STACK_BY/SHRINK_STACK_BY for calls)
  int64_t CurrentSPAdj = 0;
  // When HL is live across a pseudo expansion, we wrap the expansion in
  // PUSH H / POP H to preserve HL.  This bias accounts for the SP shift
  // from PUSH H so that all SP-relative addresses are correctly adjusted.
  int HLSaveBias = 0;
  // Pre-scanned known-zero byte masks for GR32 operands (bit i = byte i is zero).
  DenseMap<unsigned, unsigned> KnownZeroBytes;

  // Cross-operation register forwarding: after a batch-mode operation stores
  // its result in B/C/D/E to scratch, the NEXT pseudo may immediately load
  // the same bytes back. This set tracks pseudos where B/C/D/E already hold
  // the source value, so Phase 1 (load from scratch) can be skipped.
  // The unsigned value is the GR32 register that B/C/D/E hold.
  DenseMap<MachineInstr *, unsigned> BCDEForwarded;

  void preScanKnownZeroBytes(Block &MBB);
  bool expandMBB(Block &MBB);
  int64_t computeSPAdjustment(Block &MBB, BlockIt UpTo);
  bool expandMI(Block &MBB, BlockIt MBBI);
  template <unsigned OP> bool expand(Block &MBB, BlockIt MBBI);
  bool binOperationWithImmediateOperand(unsigned opCode, Block &MBB, BlockIt MBBI);
  bool binOperation(unsigned opCode, Block &MBB, BlockIt MBBI);
  int64_t getScratchOffset(unsigned Reg, int ByteIndex) const;
  // Every access to the GR32 scratch area goes through these.  Nothing else in
  // this file may compute a scratch address itself: keeping them the single
  // choke point is what makes it possible to change where the area lives (see
  // the note on getScratchOffset).  SpBias compensates for bytes pushed after
  // the offset was computed, e.g. a PUSH PSW in the middle of a sequence.
  void emitScratchAddr(Block &MBB, BlockIt MBBI, unsigned Reg, int ByteIndex,
                       int SpBias = 0);
  void emitScratchAddr(Block &MBB, const DebugLoc &DL, unsigned Reg,
                       int ByteIndex, int SpBias = 0);
  void emitScratchLoad(Block &MBB, BlockIt MBBI, unsigned Reg, int ByteIndex,
                       unsigned DestReg, int SpBias = 0);
  void emitScratchLoad(Block &MBB, const DebugLoc &DL, unsigned Reg,
                       int ByteIndex, unsigned DestReg);
  void emitScratchStore(Block &MBB, BlockIt MBBI, unsigned Reg, int ByteIndex,
                        unsigned SrcReg, int SpBias = 0);
  void emitScratchAdvance(Block &MBB, BlockIt MBBI, int Delta);

  /// Check whether the next GR32 pseudo after MBBI can consume forwarded
  /// B/C/D/E holding destReg's value, and if so, register it in
  /// BCDEForwarded and return true (meaning Phase 3 store should be skipped).
  bool tryForwardBCDE(Block &MBB, BlockIt MBBI, unsigned destReg);

  MachineInstrBuilder buildMI(Block &MBB, BlockIt MBBI, unsigned Opcode) {
    return BuildMI(MBB, MBBI, MBBI->getDebugLoc(), TII->get(Opcode));
  }

  MachineInstrBuilder buildMI(Block &MBB, BlockIt MBBI, unsigned Opcode,
                              Register DstReg) {
    return BuildMI(MBB, MBBI, MBBI->getDebugLoc(), TII->get(Opcode), DstReg);
  }

  MachineRegisterInfo &getRegInfo(Block &MBB) {
    return MBB.getParent()->getRegInfo();
  }

  bool hasAvailablePhysRegValue(Block &MBB, BlockIt MBBI, Register Reg) const {
    if (MBB.isLiveIn(Reg))
      return true;

    for (auto I = MBBI; I != MBB.begin();) {
      --I;
      bool DeadDef = false;
      for (const MachineOperand &MO : I->operands()) {
        if (!MO.isReg() || !MO.isDef())
          continue;
        Register DefReg = MO.getReg();
        if (DefReg != Reg && !TRI->isSubRegisterEq(DefReg, Reg) &&
            !TRI->isSubRegisterEq(Reg, DefReg))
          continue;
        if (!MO.isDead())
          return true;
        DeadDef = true;
      }
      // A dead def both defines and kills the register (FRMIDX's scratch use of
      // H/L, a call's clobber list), so it leaves no value behind -- and it
      // hides whatever earlier definition this walk would otherwise have found.
      if (DeadDef)
        return false;
    }

    return false;
  }

  bool hasAvailableHLValue(Block &MBB, BlockIt MBBI) const {
    return hasAvailablePhysRegValue(MBB, MBBI, I8085::HL) ||
           hasAvailablePhysRegValue(MBB, MBBI, I8085::H) ||
           hasAvailablePhysRegValue(MBB, MBBI, I8085::L);
  }

};

char I8085ExpandPseudo32::ID = 0;

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

static std::array<uint8_t, 4> splitImm32(uint64_t Imm) {
  return {static_cast<uint8_t>(Imm & 0xFF),
          static_cast<uint8_t>((Imm >> 8) & 0xFF),
          static_cast<uint8_t>((Imm >> 16) & 0xFF),
          static_cast<uint8_t>((Imm >> 24) & 0xFF)};
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

// Byte address of one byte of a GR32 imaginary register, relative to SP.
//
// This is the ONLY place that knows where the scratch area lives, and it is
// reached only through emitScratchAddr / emitScratchLoad / emitScratchStore.
// Keep it that way: relocating the area (e.g. to a fixed address for
// non-recursive functions, which removes the DAD SP and lets byte traffic use
// LDA/STA) is a change to those four functions and nothing else.  Note that
// several places in this file *do* legitimately emit `LXI H,off ; DAD SP`
// without going through here -- those address the pseudo's own memory operand
// (offsetToLoad / offsetToStore / HLSaveBias), not the scratch area, and must
// stay SP-relative.
int64_t I8085ExpandPseudo32::getScratchOffset(unsigned Reg,
                                              int ByteIndex) const {
  assert(HaveScratch && "GR32 scratch not initialized");
  // IBX normally lives at base+4, but when only IBX is used the scratch
  // slot is only 4 bytes and IBX is remapped to base+0.
  int Base = (Reg == I8085::IBX && !IBXRemapped) ? 4 : 0;
  // Add CurrentSPAdj to account for mid-function SP adjustments
  return ScratchBaseOffset + Base + ByteIndex + CurrentSPAdj + HLSaveBias;
}

void I8085ExpandPseudo32::emitScratchAddr(Block &MBB, BlockIt MBBI,
                                          unsigned Reg, int ByteIndex,
                                          int SpBias) {
  int64_t Offset = getScratchOffset(Reg, ByteIndex) + SpBias;
  buildMI(MBB, MBBI, I8085::LXI)
      .addReg(I8085::HL, RegState::Define)
      .addImm(Offset);
  buildMI(MBB, MBBI, I8085::DAD).addReg(I8085::SP);
}

void I8085ExpandPseudo32::emitScratchAddr(Block &MBB, const DebugLoc &DL,
                                          unsigned Reg, int ByteIndex,
                                          int SpBias) {
  int64_t Offset = getScratchOffset(Reg, ByteIndex) + SpBias;
  BuildMI(&MBB, DL, TII->get(I8085::LXI))
      .addReg(I8085::HL, RegState::Define)
      .addImm(Offset);
  BuildMI(&MBB, DL, TII->get(I8085::DAD)).addReg(I8085::SP);
}

void I8085ExpandPseudo32::emitScratchLoad(Block &MBB, BlockIt MBBI,
                                          unsigned Reg, int ByteIndex,
                                          unsigned DestReg, int SpBias) {
  emitScratchAddr(MBB, MBBI, Reg, ByteIndex, SpBias);
  buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(DestReg, RegState::Define);
}

void I8085ExpandPseudo32::emitScratchLoad(Block &MBB, const DebugLoc &DL,
                                          unsigned Reg, int ByteIndex,
                                          unsigned DestReg) {
  emitScratchAddr(MBB, DL, Reg, ByteIndex);
  BuildMI(&MBB, DL, TII->get(I8085::MOV_FROM_M))
      .addReg(DestReg, RegState::Define);
}

void I8085ExpandPseudo32::emitScratchStore(Block &MBB, BlockIt MBBI,
                                           unsigned Reg, int ByteIndex,
                                           unsigned SrcReg, int SpBias) {
  emitScratchAddr(MBB, MBBI, Reg, ByteIndex, SpBias);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(SrcReg);
}

void I8085ExpandPseudo32::emitScratchAdvance(Block &MBB, BlockIt MBBI,
                                             int Delta) {
  if (Delta == 0)
    return;
  unsigned Opc = (Delta > 0) ? I8085::INX : I8085::DCX;
  int Steps = (Delta > 0) ? Delta : -Delta;
  for (int i = 0; i < Steps; ++i)
    buildMI(MBB, MBBI, Opc).addReg(I8085::HL, RegState::Define).addReg(I8085::HL);
}

/// Compute the cumulative SP adjustment from block start up to (but not
/// including) the given instruction. GROW_STACK_BY decreases SP (increases
/// offset), SHRINK_STACK_BY increases SP (decreases offset).
/// We skip FrameSetup/FrameDestroy instructions as those are part of the
/// prologue/epilogue and already accounted for in ScratchBaseOffset.
int64_t I8085ExpandPseudo32::computeSPAdjustment(Block &MBB, BlockIt UpTo) {
  int64_t Adj = 0;
  for (BlockIt I = MBB.begin(); I != UpTo; ++I) {
    // Skip prologue/epilogue instructions - they're already accounted for
    if (I->getFlag(MachineInstr::FrameSetup) ||
        I->getFlag(MachineInstr::FrameDestroy))
      continue;

    unsigned Opc = I->getOpcode();
    if (Opc == I8085::GROW_STACK_BY) {
      // GROW_STACK_BY decreases SP, so we need to add to our offset
      int64_t Amount = I->getOperand(0).getImm();
      Adj += Amount;
    } else if (Opc == I8085::SHRINK_STACK_BY) {
      // SHRINK_STACK_BY increases SP, so we subtract from our offset
      int64_t Amount = I->getOperand(0).getImm();
      Adj -= Amount;
    }
  }
  return Adj;
}

/// Compute the known-zero byte mask for a single GR32-defining instruction.
/// Returns the mask (bit i set = byte i is known zero), or 0 for unrecognized
/// instructions.
static unsigned computeKnownZeroMask(const MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  unsigned mask = 0;
  if ((Opc == I8085::LOAD_32 || Opc == I8085::MVI_32) &&
      MI.getNumOperands() >= 2 && MI.getOperand(1).isImm()) {
    uint64_t imm = static_cast<uint64_t>(MI.getOperand(1).getImm()) & 0xFFFFFFFFULL;
    for (int i = 0; i < 4; i++)
      if (((imm >> (i * 8)) & 0xFF) == 0)
        mask |= (1 << i);
  } else if (Opc == I8085::ANDI_32 &&
             MI.getNumOperands() >= 3 && MI.getOperand(2).isImm()) {
    uint64_t imm = static_cast<uint64_t>(MI.getOperand(2).getImm()) & 0xFFFFFFFFULL;
    for (int i = 0; i < 4; i++)
      if (((imm >> (i * 8)) & 0xFF) == 0)
        mask |= (1 << i);
  } else if (Opc == I8085::ZEXT8TO32) {
    mask = 0x0E; // bytes 1, 2, 3 are zero
  } else if (Opc == I8085::ZEXT16TO32) {
    mask = 0x0C; // bytes 2, 3 are zero
  }
  return mask;
}

void I8085ExpandPseudo32::preScanKnownZeroBytes(Block &MBB) {
  KnownZeroBytes.clear();
  for (BlockIt MBBI = MBB.begin(), E = MBB.end(); MBBI != E; ++MBBI) {
    if (MBBI->getNumOperands() < 1 || !MBBI->getOperand(0).isReg())
      continue;
    // Only consider instructions that actually *define* operand 0.
    // Instructions that merely use GR32 as input (e.g. JMP_32_IF_NOT_EQUAL,
    // STORE_32_ADDR_CONTENT) have (outs) empty, so operand 0 is a use, not
    // a def.  Treating those as defs would overwrite legitimate masks with 0.
    if (!MBBI->getOperand(0).isDef())
      continue;
    unsigned DefReg = MBBI->getOperand(0).getReg();
    if (DefReg != I8085::IAX && DefReg != I8085::IBX)
      continue;
    unsigned mask = computeKnownZeroMask(*MBBI);
    KnownZeroBytes[DefReg] = mask;
  }
}

bool I8085ExpandPseudo32::expandMBB(MachineBasicBlock &MBB) {
  for (BlockIt MBBI = MBB.begin(), E = MBB.end(); MBBI != E; ) {
    // Compute SP adjustment up to this instruction for correct scratch offsets
    CurrentSPAdj = computeSPAdjustment(MBB, MBBI);

    // Some expansions splice instructions into new blocks, which can invalidate
    // iterators. Restart the scan after any successful expansion.
    if (expandMI(MBB, MBBI))
      return true;
    MBBI = std::next(MBBI);
  }

  return false;
}

bool I8085ExpandPseudo32::runOnMachineFunction(MachineFunction &MF) {

  LLVM_DEBUG({
    dbgs() << "********** Expand 32 bit register pseudo instructions **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  bool Modified = false;

  const I8085Subtarget &STI = MF.getSubtarget<I8085Subtarget>();
  TRI = STI.getRegisterInfo();
  TII = STI.getInstrInfo();
  HasUndoc = STI.hasUndocumented();
  I8085MachineFunctionInfo *AFI = MF.getInfo<I8085MachineFunctionInfo>();
  ScratchFI = AFI->getGR32ScratchFI();
  IBXRemapped = AFI->isIBXRemappedToZero();
  if (ScratchFI >= 0) {
    const MachineFrameInfo &MFI = MF.getFrameInfo();
    const TargetFrameLowering *TFI = STI.getFrameLowering();
    ScratchBaseOffset = MFI.getObjectOffset(ScratchFI) +
                        MFI.getStackSize() - TFI->getOffsetOfLocalArea();
    HaveScratch = true;
  }

  // We need to track liveness in order to use register scavenging.
  MF.getProperties().set(MachineFunctionProperties::Property::TracksLiveness);

  auto UsesPhysReg = [&](unsigned Reg) {
    for (auto &MBB : MF) {
      for (auto &MI : MBB) {
        for (auto &MO : MI.operands()) {
          if (MO.isReg() && MO.getReg() == Reg)
            return true;
        }
      }
    }
    return false;
  };

  bool UsesIAX = UsesPhysReg(I8085::IAX);
  bool UsesIBX = UsesPhysReg(I8085::IBX);
  if ((UsesIAX || UsesIBX) && !HaveScratch) {
    // Fallback for -run-pass tests where PEI doesn't run.
    MachineFrameInfo &MFI = MF.getFrameInfo();
    if (MFI.getObjectIndexBegin() == MFI.getObjectIndexEnd() &&
        MFI.getStackSize() == 0) {
      ScratchFI = MFI.CreateStackObject(8, Align(1), false);
      ScratchBaseOffset = 0;
      HaveScratch = true;
      AFI->setGR32ScratchFI(ScratchFI);
    } else {
      report_fatal_error("I8085 GR32 scratch slot not allocated");
    }
  }
  if (UsesIAX || UsesIBX) {
    MachineBasicBlock &Entry = MF.front();
    auto InsertPt = Entry.begin();
    DebugLoc DL = (InsertPt != Entry.end()) ? InsertPt->getDebugLoc() : DebugLoc();
    if (UsesIAX)
      BuildMI(Entry, InsertPt, DL, TII->get(TargetOpcode::IMPLICIT_DEF), I8085::IAX);
    if (UsesIBX)
      BuildMI(Entry, InsertPt, DL, TII->get(TargetOpcode::IMPLICIT_DEF), I8085::IBX);
  }

  for (Block &MBB : MF) {
    // Pre-scan to track known-zero bytes for GR32 operands, used by
    // JMP_32_IF_NOT_EQUAL to skip comparing bytes known to be zero.
    preScanKnownZeroBytes(MBB);

    // Clear cross-operation forwarding state for each new MBB.
    BCDEForwarded.clear();

    bool ContinueExpanding = true;
    unsigned ExpandCount = 0;
    unsigned MaxExpansions = static_cast<unsigned>(MBB.size()) + 16;

    // Continue expanding the block until all pseudos are expanded.
    do {
      if (ExpandCount++ >= MaxExpansions)
        report_fatal_error("I8085 pseudo expand limit reached");

      bool BlockModified = expandMBB(MBB);
      Modified |= BlockModified;
      ContinueExpanding = BlockModified;
    } while (ContinueExpanding);
  }

  for (Block &MBB : MF) {
    for (auto MBBI = MBB.begin(), E = MBB.end(); MBBI != E; ) {
      auto *MI = &*MBBI++;
      if (!MI->getDesc().isReturn())
        continue;

      bool UsesIAX = (MI->findRegisterUseOperandIdx(I8085::IAX, TRI) != -1);
      bool UsesIBX = (MI->findRegisterUseOperandIdx(I8085::IBX, TRI) != -1);
      if (UsesIAX)
        BuildMI(MBB, MI, MI->getDebugLoc(),
                TII->get(TargetOpcode::IMPLICIT_DEF), I8085::IAX);
      if (UsesIBX)
        BuildMI(MBB, MI, MI->getDebugLoc(),
                TII->get(TargetOpcode::IMPLICIT_DEF), I8085::IBX);
    }
  }

  return Modified;
}

bool I8085ExpandPseudo32::tryForwardBCDE(Block &MBB, BlockIt MBBI,
                                          unsigned destReg) {
  // Find the next GR32 pseudo immediately after MBBI in the MBB.
  // Only forward to binOperation consumers (XOR_32/OR_32/AND_32) because
  // they always write back to the same scratch slot (destReg == operandOne
  // == forwarded reg). Other consumers (MOV_32, STORE_32) would leave the
  // forwarded register's scratch stale if any later instruction reads it.
  auto NextIt = std::next(MBBI);

  // The next instruction must be immediately the consumer pseudo.
  // We don't scan past non-pseudo instructions because they could
  // clobber B/C/D/E or change control flow in ways that are hard to verify.
  if (NextIt == MBB.end())
    return false;

  MachineInstr *NextPseudo = &*NextIt;
  unsigned NextOpc = NextPseudo->getOpcode();

  // Only allow consumers whose batched expansion loads op1 from B/C/D/E in
  // Phase 1 and can therefore skip it: the bitwise binOperation ops and ADD_32
  // (which now shares that register-accumulator shape).  ADD reads op2 from
  // memory and only starts its carry chain at byte 0's ADD, so a forwarded op1
  // needs no special carry handling.
  if (NextOpc != I8085::XOR_32 && NextOpc != I8085::OR_32 &&
      NextOpc != I8085::AND_32 && NextOpc != I8085::ADD_32)
    return false;

  // Check the consumer reads from the forwarded register.
  unsigned ConsumerDest = NextPseudo->getOperand(0).getReg();
  unsigned ConsumerOp1 = NextPseudo->getOperand(1).getReg();
  unsigned ConsumerOp2 = NextPseudo->getOperand(2).getReg();

  // Batch mode requires dest == op1 (tied constraint).
  if (ConsumerDest != ConsumerOp1)
    return false;

  // The consumer's Phase 1 loads op1 into B/C/D/E. For forwarding,
  // op1 must equal the forwarded reg.
  if (ConsumerOp1 != destReg)
    return false;

  // The consumer's Phase 2 reads op2 from scratch. If op2 == destReg,
  // the scratch is stale (we skipped writing it). Bail out.
  if (ConsumerOp2 == destReg)
    return false;

  // Check that B/C/D/E are dead after the consumer too (so it can batch).
  auto AfterNext = std::next(BlockIt(NextPseudo));
  for (MCRegister Reg : {I8085::B, I8085::C, I8085::D, I8085::E}) {
    if (MBB.computeRegisterLiveness(TRI, Reg, AfterNext, 20) !=
        MachineBasicBlock::LQR_Dead)
      return false;
  }

  // All checks passed. Register the consumer for forwarding.
  LLVM_DEBUG(dbgs() << "BCDE forwarding: skip Phase 3 store of "
                    << (destReg == I8085::IAX ? "IAX" : "IBX")
                    << ", consumer will use B/C/D/E directly\n");
  BCDEForwarded[NextPseudo] = destReg;
  return true;
}

bool I8085ExpandPseudo32::binOperationWithImmediateOperand(unsigned opCode, Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  uint64_t immToAdd = MI.getOperand(2).getImm();

  auto values = splitImm32(immToAdd);

  // Track whether we've emitted the initial address computation.
  bool addrEmitted = false;
  int lastByteIdx = -1;

  for (int i = 0; i < 4; ++i) {
      uint8_t byteVal = values[i];

      // Optimize away no-op bytes:
      // - ANI 0xFF is identity (A & 0xFF = A)
      // - ORI 0x00 is identity (A | 0x00 = A)
      // - XRI 0x00 is identity (A ^ 0x00 = A)
      if ((opCode == I8085::ANI && byteVal == 0xFF) ||
          (opCode == I8085::ORI && byteVal == 0x00) ||
          (opCode == I8085::XRI && byteVal == 0x00))
        continue;

      // Emit address for this byte.
      if (!addrEmitted) {
        emitScratchAddr(MBB, MBBI, destReg, i);
        addrEmitted = true;
        lastByteIdx = i;
      } else {
        int delta = i - lastByteIdx;
        for (int d = 0; d < delta; ++d)
          emitScratchAdvance(MBB, MBBI, 1);
        lastByteIdx = i;
      }

      // Optimize constant-result bytes:
      // - ANI 0x00 always gives 0 (A & 0 = 0) -> MVI M, 0
      // - ORI 0xFF always gives 0xFF (A | 0xFF = 0xFF) -> MVI M, 0xFF
      if (opCode == I8085::ANI && byteVal == 0x00) {
        buildMI(MBB, MBBI, I8085::MVI_M).addImm(0);
        continue;
      }
      if (opCode == I8085::ORI && byteVal == 0xFF) {
        buildMI(MBB, MBBI, I8085::MVI_M).addImm(0xFF);
        continue;
      }

      // General case: load, operate, store.
      buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::A, RegState::Define);
      buildMI(MBB, MBBI, opCode).addImm(byteVal);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::A);
  }

  MI.eraseFromParent();
  return true;
}

bool I8085ExpandPseudo32::binOperation(unsigned opCode, Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operandOne = MI.getOperand(1).getReg();
  unsigned operandTwo = MI.getOperand(2).getReg();

  // Optimized path: when B/C/D/E are dead after this instruction and
  // dest == operandOne (tied constraint), batch the operations to avoid
  // redundant LXI+DAD pairs. Instead of 8 LXI+DAD (current), use only 3.
  //
  // Phase 1: Load all 4 bytes of op1 into B/C/D/E via scratch + INX
  // Phase 2: For each byte, ALU with op2 via scratch + INX, store in B/C/D/E
  // Phase 3: Store all 4 result bytes back to dest via scratch + INX
  auto AfterMI = std::next(MBBI);
  bool CanBatch = (destReg == operandOne);
  if (CanBatch) {
    for (MCRegister Reg : {I8085::B, I8085::C, I8085::D, I8085::E}) {
      if (MBB.computeRegisterLiveness(TRI, Reg, AfterMI, 20) !=
          MachineBasicBlock::LQR_Dead) {
        CanBatch = false;
        break;
      }
    }
  }

  if (CanBatch) {
    // Check if B/C/D/E already hold operandOne via forwarding.
    auto FwdIt = BCDEForwarded.find(&MI);
    bool Forwarded = (FwdIt != BCDEForwarded.end() && FwdIt->second == operandOne);
    if (Forwarded) {
      LLVM_DEBUG(dbgs() << "BCDE forwarding: skip Phase 1 load of "
                        << (operandOne == I8085::IAX ? "IAX" : "IBX")
                        << " in binOperation\n");
      BCDEForwarded.erase(FwdIt);
    }

    if (!Forwarded) {
      // Phase 1: Load all 4 bytes of op1 into B, C, D, E
      emitScratchAddr(MBB, MBBI, operandOne, 0);
      buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::B, RegState::Define);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::C, RegState::Define);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::D, RegState::Define);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::E, RegState::Define);
    }

    // Phase 2: ALU each byte with op2, results back into B/C/D/E
    emitScratchAddr(MBB, MBBI, operandTwo, 0);
    // Byte 0: B = B op [HL]
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::A, RegState::Define)
        .addReg(I8085::B);
    buildMI(MBB, MBBI, opCode);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::B, RegState::Define)
        .addReg(I8085::A);
    emitScratchAdvance(MBB, MBBI, 1);
    // Byte 1: C = C op [HL]
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::A, RegState::Define)
        .addReg(I8085::C);
    buildMI(MBB, MBBI, opCode);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::C, RegState::Define)
        .addReg(I8085::A);
    emitScratchAdvance(MBB, MBBI, 1);
    // Byte 2: D = D op [HL]
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::A, RegState::Define)
        .addReg(I8085::D);
    buildMI(MBB, MBBI, opCode);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::D, RegState::Define)
        .addReg(I8085::A);
    emitScratchAdvance(MBB, MBBI, 1);
    // Byte 3: E = E op [HL]
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::A, RegState::Define)
        .addReg(I8085::E);
    buildMI(MBB, MBBI, opCode);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(I8085::E, RegState::Define)
        .addReg(I8085::A);

    // Phase 3: Store all 4 result bytes back to dest, unless we can
    // forward B/C/D/E to the next pseudo.
    if (!tryForwardBCDE(MBB, MBBI, destReg)) {
      emitScratchAddr(MBB, MBBI, destReg, 0);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::B);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::C);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::D);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::E);
    }
  } else {
    // Fallback: original per-byte approach with full address recomputation
    for(int i=0;i<4;i++){
        emitScratchLoad(MBB, MBBI, operandOne, i, I8085::A);
        emitScratchAddr(MBB, MBBI, operandTwo, i);
        buildMI(MBB, MBBI, opCode);
        emitScratchStore(MBB, MBBI, destReg, i, I8085::A);
    }
  }

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo32::expand<I8085::XOR_32>(Block &MBB, BlockIt MBBI) {
  return binOperation(I8085::XRA_M,MBB,MBBI);
}
template <> bool I8085ExpandPseudo32::expand<I8085::OR_32>(Block &MBB, BlockIt MBBI) {
  return binOperation(I8085::ORA_M,MBB,MBBI);
}
template <> bool I8085ExpandPseudo32::expand<I8085::AND_32>(Block &MBB, BlockIt MBBI) {
  return binOperation(I8085::ANA_M,MBB,MBBI);
}

template <> bool I8085ExpandPseudo32::expand<I8085::XORI_32>(Block &MBB, BlockIt MBBI) {
  return binOperationWithImmediateOperand(I8085::XRI,MBB,MBBI);
}
template <> bool I8085ExpandPseudo32::expand<I8085::ORI_32>(Block &MBB, BlockIt MBBI) {
  return binOperationWithImmediateOperand(I8085::ORI,MBB,MBBI);
}
template <> bool I8085ExpandPseudo32::expand<I8085::ANDI_32>(Block &MBB, BlockIt MBBI) {
  return binOperationWithImmediateOperand(I8085::ANI,MBB,MBBI);
}


template <> bool I8085ExpandPseudo32::expand<I8085::RR_32>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();
  if (destReg != srcReg) {
    for (int i = 0; i < 4; ++i) {
      emitScratchLoad(MBB, MBBI, srcReg, i, I8085::A);
      emitScratchStore(MBB, MBBI, destReg, i, I8085::A);
    }
  }

  // No explicit carry-clear here: emitScratchAddr's `DAD SP` (right below)
  // overwrites CY before the first RAR reads it, so the byte-3 bit that RAR
  // pulls in from CY is arbitrary - the trailing `ANI 127` mask below clears
  // exactly that bit. (An `ORA A` here would be dead AND read an undefined A.)
  emitScratchAddr(MBB, MBBI, destReg, 3);
  for(int i=3;i>-1;i--){
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::A, RegState::Define);
    buildMI(MBB, MBBI, I8085::RAR);
    buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::A);
    if (i != 0)
      emitScratchAdvance(MBB, MBBI, -1);
  }

  emitScratchAddr(MBB, MBBI, destReg, 3);
  buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::A, RegState::Define);
  buildMI(MBB, MBBI, I8085::ANI).addImm(127);  
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::A);
  
  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo32::expand<I8085::ASR_32>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();
  if (destReg != srcReg) {
    for (int i = 0; i < 4; ++i) {
      emitScratchLoad(MBB, MBBI, srcReg, i, I8085::A);
      emitScratchStore(MBB, MBBI, destReg, i, I8085::A);
    }
  }

  // Set carry from sign bit of the high byte.
  emitScratchAddr(MBB, MBBI, destReg, 3);
  buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::A, RegState::Define);
  buildMI(MBB, MBBI, I8085::RLC);

  // Shift high byte through carry to preserve sign (keep carry intact).
  buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::A, RegState::Define);
  buildMI(MBB, MBBI, I8085::RAR);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::A);

  emitScratchAdvance(MBB, MBBI, -1);
  for(int i=2;i>-1;i--){
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::A, RegState::Define);
    buildMI(MBB, MBBI, I8085::RAR);
    buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::A);
    if (i != 0)
      emitScratchAdvance(MBB, MBBI, -1);
  }

  MI.eraseFromParent();
  return true;
}

// --- Byte-shuffle shift expansions ---
// shl i32, 8:  [b0,b1,b2,b3] -> [0,b0,b1,b2]
template <> bool I8085ExpandPseudo32::expand<I8085::SHL_32_BY_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  // Read bytes 2,1,0 from src then write to dest bytes 3,2,1; dest byte 0 = 0.
  // We must read all source bytes before writing in case src == dest.
  // Use B,C,D to hold bytes 0,1,2 of src.
  emitScratchLoad(MBB, MBBI, srcReg, 2, I8085::D);
  emitScratchLoad(MBB, MBBI, srcReg, 1, I8085::C);
  emitScratchLoad(MBB, MBBI, srcReg, 0, I8085::B);

  // Write: dest[3]=src[2], dest[2]=src[1], dest[1]=src[0], dest[0]=0
  emitScratchStore(MBB, MBBI, destReg, 3, I8085::D);
  emitScratchStore(MBB, MBBI, destReg, 2, I8085::C);
  emitScratchStore(MBB, MBBI, destReg, 1, I8085::B);
  emitScratchAddr(MBB, MBBI, destReg, 0);
  buildMI(MBB, MBBI, I8085::MVI_M).addImm(0);

  MI.eraseFromParent();
  return true;
}

// shl i32, 16: [b0,b1,b2,b3] -> [0,0,b0,b1]
template <> bool I8085ExpandPseudo32::expand<I8085::SHL_32_BY_16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  emitScratchLoad(MBB, MBBI, srcReg, 1, I8085::C);
  emitScratchLoad(MBB, MBBI, srcReg, 0, I8085::B);

  emitScratchStore(MBB, MBBI, destReg, 3, I8085::C);
  emitScratchStore(MBB, MBBI, destReg, 2, I8085::B);
  emitScratchAddr(MBB, MBBI, destReg, 0);
  buildMI(MBB, MBBI, I8085::MVI_M).addImm(0);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MVI_M).addImm(0);

  MI.eraseFromParent();
  return true;
}

// shl i32, 24: [b0,b1,b2,b3] -> [0,0,0,b0]
template <> bool I8085ExpandPseudo32::expand<I8085::SHL_32_BY_24>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  emitScratchLoad(MBB, MBBI, srcReg, 0, I8085::B);

  emitScratchStore(MBB, MBBI, destReg, 3, I8085::B);
  emitScratchAddr(MBB, MBBI, destReg, 0);
  buildMI(MBB, MBBI, I8085::MVI_M).addImm(0);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MVI_M).addImm(0);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MVI_M).addImm(0);

  MI.eraseFromParent();
  return true;
}

// lshr i32, 8:  [b0,b1,b2,b3] -> [b1,b2,b3,0]
template <> bool I8085ExpandPseudo32::expand<I8085::SRL_32_BY_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  emitScratchLoad(MBB, MBBI, srcReg, 1, I8085::B);
  emitScratchLoad(MBB, MBBI, srcReg, 2, I8085::C);
  emitScratchLoad(MBB, MBBI, srcReg, 3, I8085::D);

  emitScratchStore(MBB, MBBI, destReg, 0, I8085::B);
  emitScratchStore(MBB, MBBI, destReg, 1, I8085::C);
  emitScratchStore(MBB, MBBI, destReg, 2, I8085::D);
  emitScratchAddr(MBB, MBBI, destReg, 3);
  buildMI(MBB, MBBI, I8085::MVI_M).addImm(0);

  MI.eraseFromParent();
  return true;
}

// lshr i32, 16: [b0,b1,b2,b3] -> [b2,b3,0,0]
template <> bool I8085ExpandPseudo32::expand<I8085::SRL_32_BY_16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  emitScratchLoad(MBB, MBBI, srcReg, 2, I8085::B);
  emitScratchLoad(MBB, MBBI, srcReg, 3, I8085::C);

  emitScratchStore(MBB, MBBI, destReg, 0, I8085::B);
  emitScratchStore(MBB, MBBI, destReg, 1, I8085::C);
  emitScratchAddr(MBB, MBBI, destReg, 2);
  buildMI(MBB, MBBI, I8085::MVI_M).addImm(0);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MVI_M).addImm(0);

  MI.eraseFromParent();
  return true;
}

// lshr i32, 24: [b0,b1,b2,b3] -> [b3,0,0,0]
template <> bool I8085ExpandPseudo32::expand<I8085::SRL_32_BY_24>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  emitScratchLoad(MBB, MBBI, srcReg, 3, I8085::B);

  emitScratchStore(MBB, MBBI, destReg, 0, I8085::B);
  emitScratchAddr(MBB, MBBI, destReg, 1);
  buildMI(MBB, MBBI, I8085::MVI_M).addImm(0);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MVI_M).addImm(0);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MVI_M).addImm(0);

  MI.eraseFromParent();
  return true;
}

// ashr i32, 8:  [b0,b1,b2,b3] -> [b1,b2,b3,sign] where sign = (b3 & 0x80) ? 0xFF : 0x00
template <> bool I8085ExpandPseudo32::expand<I8085::ASR_32_BY_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  // Read bytes we need
  emitScratchLoad(MBB, MBBI, srcReg, 1, I8085::B);
  emitScratchLoad(MBB, MBBI, srcReg, 2, I8085::C);
  emitScratchLoad(MBB, MBBI, srcReg, 3, I8085::D);

  // Compute sign extension byte from D (src byte 3)
  // ADI 128 sets carry if high bit was set; SBB A gives 0xFF if carry, 0x00 otherwise
  buildMI(MBB, MBBI, I8085::MOV).addReg(I8085::A, RegState::Define).addReg(I8085::D);
  buildMI(MBB, MBBI, I8085::ADI).addImm(128);
  buildMI(MBB, MBBI, I8085::SBB).addReg(I8085::A);

  // Write dest
  emitScratchStore(MBB, MBBI, destReg, 0, I8085::B);
  emitScratchStore(MBB, MBBI, destReg, 1, I8085::C);
  emitScratchStore(MBB, MBBI, destReg, 2, I8085::D);
  emitScratchStore(MBB, MBBI, destReg, 3, I8085::A); // sign byte

  MI.eraseFromParent();
  return true;
}

// ashr i32, 16: [b0,b1,b2,b3] -> [b2,b3,sign,sign]
template <> bool I8085ExpandPseudo32::expand<I8085::ASR_32_BY_16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  emitScratchLoad(MBB, MBBI, srcReg, 2, I8085::B);
  emitScratchLoad(MBB, MBBI, srcReg, 3, I8085::C);

  // Compute sign from C (src byte 3)
  buildMI(MBB, MBBI, I8085::MOV).addReg(I8085::A, RegState::Define).addReg(I8085::C);
  buildMI(MBB, MBBI, I8085::ADI).addImm(128);
  buildMI(MBB, MBBI, I8085::SBB).addReg(I8085::A);

  emitScratchStore(MBB, MBBI, destReg, 0, I8085::B);
  emitScratchStore(MBB, MBBI, destReg, 1, I8085::C);
  emitScratchStore(MBB, MBBI, destReg, 2, I8085::A);
  emitScratchStore(MBB, MBBI, destReg, 3, I8085::A);

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo32::expand<I8085::STORE_32_ADDR_CONTENT>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned addrReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  bool addrIsHL = (addrReg == I8085::HL);
  bool addrIsSP = (addrReg == I8085::SP);
  unsigned addrLow = 0, addrHigh = 0;
  bool haveAddr = true;
  if (!addrIsSP) {
    if (addrReg == I8085::BC) {
      addrLow = I8085::C;
      addrHigh = I8085::B;
    } else if (addrReg == I8085::DE) {
      addrLow = I8085::E;
      addrHigh = I8085::D;
    } else if (addrReg == I8085::HL) {
      addrLow = I8085::L;
      addrHigh = I8085::H;
    } else {
      haveAddr = false;
    }
  }

  if (!haveAddr)
    return false;

  auto emitScratchLoadWithBias = [&](int SpBias, unsigned Reg, int ByteIndex,
                                     unsigned DestReg) {
    emitScratchLoad(MBB, MBBI, Reg, ByteIndex, DestReg, SpBias);
  };

  if (addrIsHL)
    buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::HL);

  for(int i=0;i<4;i++){
    // Load byte i from the 32-bit source.
    if (addrIsHL) {
      // SP is biased by the saved HL (PUSH) in this path.
      emitScratchLoadWithBias(/*SpBias=*/2, srcReg, i, I8085::A);
    } else {
      emitScratchLoad(MBB, MBBI, srcReg, i, I8085::A);
    }

    if (addrIsHL) {
      buildMI(MBB, MBBI, I8085::POP).addReg(I8085::HL);
    } else if (addrIsSP) {
      buildMI(MBB, MBBI, I8085::LXI).addReg(I8085::HL,RegState::Define).addImm(HLSaveBias);
      buildMI(MBB, MBBI, I8085::DAD).addReg(I8085::SP);
    } else {
      buildMI(MBB, MBBI, I8085::MOV).addReg(I8085::H, RegState::Define).addReg(addrHigh);
      buildMI(MBB, MBBI, I8085::MOV).addReg(I8085::L, RegState::Define).addReg(addrLow);
    }

    if (!addrIsHL) {
      for (int j = 0; j < i; ++j)
        buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define).addReg(I8085::HL);
    }

    buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::A);

    if (addrIsHL && i < 3) {
      buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define).addReg(I8085::HL);
      buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::HL);
    }
  }

  MI.eraseFromParent();
  return true;
}


template <> bool I8085ExpandPseudo32::expand<I8085::RL_32>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();
  if (destReg != srcReg) {
    for (int i = 0; i < 4; ++i) {
      emitScratchLoad(MBB, MBBI, srcReg, i, I8085::A);
      emitScratchStore(MBB, MBBI, destReg, i, I8085::A);
    }
  }

  // No explicit carry-clear here: emitScratchAddr's `DAD SP` (right below)
  // overwrites CY before the first RAL reads it, so the byte-0 bit that RAL
  // pulls in from CY is arbitrary - the trailing `ANI 254` mask below clears
  // exactly that bit. (An `ORA A` here would be dead AND read an undefined A.)
  emitScratchAddr(MBB, MBBI, destReg, 0);
  for(int i=0;i<4;i++){
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::A, RegState::Define);
    buildMI(MBB, MBBI, I8085::RAL);
    buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::A);
    if (i != 3)
      emitScratchAdvance(MBB, MBBI, 1);
  }

  emitScratchAddr(MBB, MBBI, destReg, 0);
  buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::A, RegState::Define);
  buildMI(MBB, MBBI, I8085::ANI).addImm(254);  
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::A);

  MI.eraseFromParent();
  return true;
}


template <> bool I8085ExpandPseudo32::expand<I8085::SEXT32_INREG_8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned srcReg = MI.getOperand(0).getReg();
  emitScratchLoad(MBB, MBBI, srcReg, 0, I8085::A);

  buildMI(MBB, MBBI, I8085::ADI)
    .addImm(128); // 80h in hex .. [Adding 80h will set carry flag if HSB is high]

  buildMI(MBB, MBBI, I8085::SBB)
    .addReg(I8085::A);  // will result in FFh if CF set, 0 else

  emitScratchAddr(MBB, MBBI, srcReg, 1);
  for (int i = 0; i < 3; ++i) {
    buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::A);
    if (i != 2)
      emitScratchAdvance(MBB, MBBI, 1);
  }

  MI.eraseFromParent();
  return true;
}


template <> bool I8085ExpandPseudo32::expand<I8085::SEXT32_INREG_16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned srcReg = MI.getOperand(0).getReg();
  emitScratchLoad(MBB, MBBI, srcReg, 1, I8085::A);

  buildMI(MBB, MBBI, I8085::ADI)
    .addImm(128); // 80h in hex .. [Adding 80h will set carry flag if HSB is high]

  buildMI(MBB, MBBI, I8085::SBB)
    .addReg(I8085::A);  // will result in FFh if CF set, 0 else

  emitScratchAddr(MBB, MBBI, srcReg, 2);
  for (int i = 0; i < 2; ++i) {
    buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::A);
    if (i != 1)
      emitScratchAdvance(MBB, MBBI, 1);
  }

  MI.eraseFromParent();
  return true;
}


template <> bool I8085ExpandPseudo32::expand<I8085::TRUNC32TO16>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  unsigned destLow = 0, destHigh = 0;

  if(destReg==I8085::BC){  destLow=I8085::C;  destHigh=I8085::B; }
  if(destReg==I8085::DE){  destLow=I8085::E;  destHigh=I8085::D; }
  if(destReg==I8085::HL){  destLow=I8085::L;  destHigh=I8085::H; }

  if (destReg == I8085::HL) {
    emitScratchLoad(MBB, MBBI, srcReg, 0, destLow);
    emitScratchLoad(MBB, MBBI, srcReg, 1, destHigh);
  } else {
    emitScratchAddr(MBB, MBBI, srcReg, 0);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(destLow, RegState::Define);
    emitScratchAdvance(MBB, MBBI, 1);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(destHigh, RegState::Define);
  }

  MI.eraseFromParent();
  return true;
}

// Extract high word (bytes 2-3) from a 32-bit value.
// This avoids the costly SRL by 16 that corrupts values at O2.
template <> bool I8085ExpandPseudo32::expand<I8085::TRUNC32TO16_HI>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  unsigned destLow = 0, destHigh = 0;

  if(destReg==I8085::BC){  destLow=I8085::C;  destHigh=I8085::B; }
  if(destReg==I8085::DE){  destLow=I8085::E;  destHigh=I8085::D; }
  if(destReg==I8085::HL){  destLow=I8085::L;  destHigh=I8085::H; }

  // Load bytes 2 and 3 (the high word) instead of bytes 0 and 1
  if (destReg == I8085::HL) {
    emitScratchLoad(MBB, MBBI, srcReg, 2, destLow);
    emitScratchLoad(MBB, MBBI, srcReg, 3, destHigh);
  } else {
    emitScratchAddr(MBB, MBBI, srcReg, 2);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(destLow, RegState::Define);
    emitScratchAdvance(MBB, MBBI, 1);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(destHigh, RegState::Define);
  }

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo32::expand<I8085::SEXT16TO32>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  unsigned opOneLow = 0, opOneHigh = 0;
  if (srcReg == I8085::BC) {
    opOneLow = I8085::C;
    opOneHigh = I8085::B;
  } else if (srcReg == I8085::DE) {
    opOneLow = I8085::E;
    opOneHigh = I8085::D;
  } else if (srcReg == I8085::HL) {
    opOneLow = I8085::L;
    opOneHigh = I8085::H;
  }

  emitScratchAddr(MBB, MBBI, destReg, 0);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(opOneLow);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(opOneHigh);

  buildMI(MBB, MBBI, I8085::MOV).addReg(I8085::A,RegState::Define).addReg(opOneHigh);

  buildMI(MBB, MBBI, I8085::ADI)
    .addImm(128); // 80h in hex .. [Adding 80h will set carry flag if HSB is high]

  buildMI(MBB, MBBI, I8085::SBB)
    .addReg(I8085::A);  // will result in FFh if CF set, 0 else

  emitScratchAddr(MBB, MBBI, destReg, 2);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::A);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::A);

  MI.eraseFromParent();
  return true;
}


template <> bool I8085ExpandPseudo32::expand<I8085::ZEXT16TO32>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  unsigned opOneLow = 0, opOneHigh = 0;

  if(srcReg==I8085::BC){  opOneLow=I8085::C;  opOneHigh=I8085::B; }
  if(srcReg==I8085::DE){  opOneLow=I8085::E;  opOneHigh=I8085::D; }
  if(srcReg==I8085::HL){  opOneLow=I8085::L;  opOneHigh=I8085::H; }

  emitScratchAddr(MBB, MBBI, destReg, 0);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(opOneLow);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(opOneHigh);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MVI_M).addImm(0);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MVI_M).addImm(0);

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo32::expand<I8085::AEXT16TO32>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  return expand<I8085::ZEXT16TO32>(MBB, MI);
}


template <> bool I8085ExpandPseudo32::expand<I8085::TRUNC32TO8>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();
  emitScratchLoad(MBB, MBBI, srcReg, 0, destReg);

  MI.eraseFromParent();
  return true;
}


template <> bool I8085ExpandPseudo32::expand<I8085::SEXT8TO32>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();
  emitScratchAddr(MBB, MBBI, destReg, 0);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(srcReg);

  buildMI(MBB, MBBI, I8085::MOV).addReg(I8085::A,RegState::Define).addReg(srcReg);

  buildMI(MBB, MBBI, I8085::ADI)
    .addImm(128); // 80h in hex .. [Adding 80h will set carry flag if HSB is high]

  buildMI(MBB, MBBI, I8085::SBB)
    .addReg(I8085::A);  // will result in FFh if CF set, 0 else


  emitScratchAddr(MBB, MBBI, destReg, 1);
  for (int i = 0; i < 3; ++i) {
    buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::A);
    if (i != 2)
      emitScratchAdvance(MBB, MBBI, 1);
  }

  MI.eraseFromParent();
  return true;
}


template <> bool I8085ExpandPseudo32::expand<I8085::ZEXT8TO32>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();
  emitScratchAddr(MBB, MBBI, destReg, 0);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(srcReg);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MVI_M).addImm(0);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MVI_M).addImm(0);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MVI_M).addImm(0);

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo32::expand<I8085::AEXT8TO32>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  return expand<I8085::ZEXT8TO32>(MBB, MI);
}

template <> bool I8085ExpandPseudo32::expand<I8085::MOV_32>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  // Check if B, C, D, E are all dead after this instruction.
  // If so, batch-load all 4 bytes into registers, then batch-store,
  // using INX H between sequential accesses (saves 6 LXI+DAD pairs).
  auto AfterMI = std::next(MBBI);
  bool CanBatch = true;
  for (MCRegister Reg : {I8085::B, I8085::C, I8085::D, I8085::E}) {
    if (MBB.computeRegisterLiveness(TRI, Reg, AfterMI, 20) !=
        MachineBasicBlock::LQR_Dead) {
      CanBatch = false;
      break;
    }
  }

  if (CanBatch) {
    // Load all 4 bytes from source into B/C/D/E
    emitScratchAddr(MBB, MBBI, srcReg, 0);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::B, RegState::Define);
    emitScratchAdvance(MBB, MBBI, 1);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::C, RegState::Define);
    emitScratchAdvance(MBB, MBBI, 1);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::D, RegState::Define);
    emitScratchAdvance(MBB, MBBI, 1);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::E, RegState::Define);

    // Store all 4 bytes to destination from B/C/D/E, unless we can
    // forward to the next pseudo.
    if (!tryForwardBCDE(MBB, MBBI, destReg)) {
      emitScratchAddr(MBB, MBBI, destReg, 0);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::B);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::C);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::D);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::E);
    }
  } else {
    for (int i = 0; i < 4; i++) {
      emitScratchLoad(MBB, MBBI, srcReg, i, I8085::A);
      emitScratchStore(MBB, MBBI, destReg, i, I8085::A);
    }
  }

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo32::expand<I8085::PACK_16_TO_32>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned loReg = MI.getOperand(1).getReg();
  unsigned hiReg = MI.getOperand(2).getReg();

  unsigned loLow = 0, loHigh = 0, hiLow = 0, hiHigh = 0;
  if (!getPairRegs(loReg, loLow, loHigh) || !getPairRegs(hiReg, hiLow, hiHigh))
    return false;

  auto copyHLToPair = [&](unsigned Pair) {
    unsigned PairLow = 0, PairHigh = 0;
    // NOTE: getPairRegs has the side effect of filling PairLow/PairHigh via
    // out-params - it must NOT live inside assert(), which is compiled out in
    // release builds (leaving the temps as $noreg and corrupting the store).
    bool GotPair = getPairRegs(Pair, PairLow, PairHigh);
    assert(GotPair && "expected 16-bit pair register");
    (void)GotPair;
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(PairLow, RegState::Define)
        .addReg(I8085::L);
    buildMI(MBB, MBBI, I8085::MOV)
        .addReg(PairHigh, RegState::Define)
        .addReg(I8085::H);
    return std::pair<unsigned, unsigned>(PairLow, PairHigh);
  };

  // emitScratchAddr() clobbers HL. If either source pair is HL, copy it to
  // the remaining free 16-bit pair first so we store the original value, not
  // the computed scratch address bytes.
  if (loReg == I8085::HL && hiReg == I8085::HL) {
    auto LoPair = copyHLToPair(I8085::BC);
    auto HiPair = copyHLToPair(I8085::DE);
    loLow = LoPair.first;
    loHigh = LoPair.second;
    hiLow = HiPair.first;
    hiHigh = HiPair.second;
  } else if (loReg == I8085::HL) {
    auto Saved = copyHLToPair(hiReg == I8085::BC ? I8085::DE : I8085::BC);
    loLow = Saved.first;
    loHigh = Saved.second;
  } else if (hiReg == I8085::HL) {
    auto Saved = copyHLToPair(loReg == I8085::BC ? I8085::DE : I8085::BC);
    hiLow = Saved.first;
    hiHigh = Saved.second;
  }

  emitScratchAddr(MBB, MBBI, destReg, 0);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(loLow);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(loHigh);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(hiLow);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(hiHigh);

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo32::expand<I8085::PACK_BCDE_TO_32>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();

  // Hardcoded: BC is lo16, DE is hi16.
  // Store C, B, E, D into scratch bytes 0-3.
  emitScratchAddr(MBB, MBBI, destReg, 0);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::C);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::B);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::E);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::D);

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo32::expand<I8085::BSWAP32>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  // BSWAP32: reverse all 4 bytes
  // bytes: [b0, b1, b2, b3] -> [b3, b2, b1, b0]
  // Load source bytes, store in reverse order to dest

  // Load all 4 bytes from src using scratch space
  emitScratchAddr(MBB, MBBI, srcReg, 0);
  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::B, RegState::Define)
    .addReg(I8085::M);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::C, RegState::Define)
    .addReg(I8085::M);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::D, RegState::Define)
    .addReg(I8085::M);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MOV)
    .addReg(I8085::E, RegState::Define)
    .addReg(I8085::M);

  // Now B=b0, C=b1, D=b2, E=b3
  // Store in reverse order: dest = [E, D, C, B] = [b3, b2, b1, b0]
  emitScratchAddr(MBB, MBBI, destReg, 0);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::E);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::D);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::C);
  emitScratchAdvance(MBB, MBBI, 1);
  buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::B);

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo32::expand<I8085::STORE_32>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned baseReg = MI.getOperand(0).getReg();
  int offsetToStore = MI.getOperand(1).getImm();
  unsigned srcReg = MI.getOperand(2).getReg();

  // Adjust destination offset for HL preservation (PUSH H shifts SP by -2).
  if (HLSaveBias > 0 && baseReg == I8085::SP)
    offsetToStore += HLSaveBias;

  auto bumpHL = [&](int Steps, unsigned Opc) {
    for (int i = 0; i < Steps; ++i)
      buildMI(MBB, MBBI, Opc).addReg(I8085::HL, RegState::Define).addReg(I8085::HL);
  };

  // If the base is HL, adjust HL in place; this pseudo clobbers HL anyway.
  if (baseReg == I8085::HL) {
    if (offsetToStore != 0) {
      unsigned Opc = (offsetToStore > 0) ? I8085::INX : I8085::DCX;
      int Steps = (offsetToStore > 0) ? offsetToStore : -offsetToStore;
      bumpHL(Steps, Opc);
    }
    for (int i = 0; i < 4; ++i) {
      emitScratchLoad(MBB, MBBI, srcReg, i, I8085::A);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::A);
      if (i != 3)
        buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL, RegState::Define).addReg(I8085::HL);
    }
    MI.eraseFromParent();
    return true;
  }

  // Check if B, C, D, E are all dead after this instruction.
  // If so, batch-load from scratch into B/C/D/E, then batch-store to dest.
  {
    auto AfterMI = std::next(MBBI);
    bool CanBatch = true;
    for (MCRegister Reg : {I8085::B, I8085::C, I8085::D, I8085::E}) {
      if (MBB.computeRegisterLiveness(TRI, Reg, AfterMI, 20) !=
          MachineBasicBlock::LQR_Dead) {
        CanBatch = false;
        break;
      }
    }

    if (CanBatch) {
      // Phase 1: Load 4 bytes from scratch into B/C/D/E
      emitScratchAddr(MBB, MBBI, srcReg, 0);
      buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::B, RegState::Define);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::C, RegState::Define);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::D, RegState::Define);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::E, RegState::Define);

      // Phase 2: Store B/C/D/E to [baseReg+offset] via one LXI+DAD + INX chain
      buildMI(MBB, MBBI, I8085::LXI).addReg(I8085::HL, RegState::Define).addImm(offsetToStore);
      buildMI(MBB, MBBI, I8085::DAD).addReg(baseReg);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::B);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::C);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::D);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::E);
    } else {
      // Fallback: original per-byte approach with full address recomputation
      for (int i = 0; i < 4; ++i) {
        emitScratchLoad(MBB, MBBI, srcReg, i, I8085::A);
        buildMI(MBB, MBBI, I8085::LXI)
            .addReg(I8085::HL, RegState::Define)
            .addImm(offsetToStore + i);
        buildMI(MBB, MBBI, I8085::DAD).addReg(baseReg);
        buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::A);
      }
    }
  }

  MI.eraseFromParent();
  return true;
}


template <> bool I8085ExpandPseudo32::expand<I8085::ADD_32>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operandOne = MI.getOperand(1).getReg();
  unsigned operandTwo = MI.getOperand(2).getReg();

  // Batched path (the hand-written-runtime technique): keep the accumulator
  // (op1) in B/C/D/E across the whole add and read op2 from memory one byte at
  // a time -- one memory access per byte instead of the three (load dest, add,
  // store dest) plus the DCX*4/INX*4 walk between the two scratch operands that
  // the byte-in-memory form below pays.  This mirrors binOperation() but chains
  // carry: byte 0 is ADD, bytes 1-3 are ADC.  INX (emitScratchAdvance) and MOV
  // preserve the carry flag, so advancing to the next op2 byte is safe; the DAD
  // inside emitScratchAddr clobbers carry but only runs before byte 0's ADD.
  // Requires dest==op1 (the tied constraint) and B/C/D/E dead afterwards.
  auto AfterMI = std::next(MBBI);
  bool CanBatch = (destReg == operandOne);
  if (CanBatch) {
    for (MCRegister Reg : {I8085::B, I8085::C, I8085::D, I8085::E}) {
      if (MBB.computeRegisterLiveness(TRI, Reg, AfterMI, 20) !=
          MachineBasicBlock::LQR_Dead) {
        CanBatch = false;
        break;
      }
    }
  }

  if (CanBatch) {
    // Phase 1: load op1 into B, C, D, E (B = byte 0 = LSB).  Skipped when a
    // previous pseudo forwarded op1 into B/C/D/E.
    auto FwdIt = BCDEForwarded.find(&MI);
    bool Forwarded = (FwdIt != BCDEForwarded.end() && FwdIt->second == operandOne);
    if (Forwarded)
      BCDEForwarded.erase(FwdIt);
    if (!Forwarded) {
      emitScratchAddr(MBB, MBBI, operandOne, 0);
      buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::B, RegState::Define);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::C, RegState::Define);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::D, RegState::Define);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::E, RegState::Define);
    }

    // Phase 2: add op2 into B/C/D/E with a carry chain (ADD then ADC*3).
    static const unsigned Regs[4] = {I8085::B, I8085::C, I8085::D, I8085::E};
    emitScratchAddr(MBB, MBBI, operandTwo, 0);
    for (int i = 0; i < 4; ++i) {
      buildMI(MBB, MBBI, I8085::MOV).addReg(I8085::A, RegState::Define).addReg(Regs[i]);
      buildMI(MBB, MBBI, i == 0 ? I8085::ADD_M : I8085::ADC_M);
      buildMI(MBB, MBBI, I8085::MOV).addReg(Regs[i], RegState::Define).addReg(I8085::A);
      if (i != 3)
        emitScratchAdvance(MBB, MBBI, 1);
    }

    // Phase 3: store B/C/D/E back to dest, unless it can be forwarded onward.
    if (!tryForwardBCDE(MBB, MBBI, destReg)) {
      emitScratchAddr(MBB, MBBI, destReg, 0);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::B);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::C);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::D);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::E);
    }

    MI.eraseFromParent();
    return true;
  }

  // Fallback: byte-in-memory carry-safe form.  Each byte loads dest[i] into A,
  // walks to op2[i] (INX/DCX preserve carry), ADCs, walks back and stores.
  unsigned fbDest = MI.getOperand(0).getReg();
  int baseOne = (fbDest == I8085::IBX) ? 4 : 0;
  int baseTwo = (operandTwo == I8085::IBX) ? 4 : 0;
  int delta = baseTwo - baseOne;
  int deltaSteps = (delta < 0) ? -delta : delta;
  unsigned deltaOpc = (delta > 0) ? I8085::INX : I8085::DCX;
  unsigned deltaBackOpc = (delta > 0) ? I8085::DCX : I8085::INX;

  auto bumpHL = [&](int Steps, unsigned Opc) {
    for (int i = 0; i < Steps; ++i)
      buildMI(MBB, MBBI, Opc).addReg(I8085::HL, RegState::Define).addReg(I8085::HL);
  };

  emitScratchAddr(MBB, MBBI, fbDest, 0);
  for (int i = 0; i < 4; ++i) {
      buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::A, RegState::Define);
      if (delta != 0)
        bumpHL(deltaSteps, deltaOpc);
      if (i > 0)
        buildMI(MBB, MBBI, I8085::ADC_M);
      else
        buildMI(MBB, MBBI, I8085::ADD_M);
      if (delta != 0)
        bumpHL(deltaSteps, deltaBackOpc);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::A);
      if (i != 3)
        emitScratchAdvance(MBB, MBBI, 1);
  }

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo32::expand<I8085::SUBI_32>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  uint64_t immToAdd = MI.getOperand(2).getImm();

  auto values = splitImm32(immToAdd);

  emitScratchAddr(MBB, MBBI, destReg, 0);
  for (int i = 0; i < 4; ++i) {
      buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::A, RegState::Define);
      if (i > 0)
        buildMI(MBB, MBBI, I8085::SBI).addImm(values[i]);
      else
        buildMI(MBB, MBBI, I8085::SUI).addImm(values[i]);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::A);
      if (i != 3)
        emitScratchAdvance(MBB, MBBI, 1);
  }

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo32::expand<I8085::SUB_32>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned operandTwo = MI.getOperand(2).getReg();

  int baseOne = (destReg == I8085::IBX) ? 4 : 0;
  int baseTwo = (operandTwo == I8085::IBX) ? 4 : 0;
  int delta = baseTwo - baseOne;
  int deltaSteps = (delta < 0) ? -delta : delta;
  unsigned deltaOpc = (delta > 0) ? I8085::INX : I8085::DCX;
  unsigned deltaBackOpc = (delta > 0) ? I8085::DCX : I8085::INX;

  auto bumpHL = [&](int Steps, unsigned Opc) {
    for (int i = 0; i < Steps; ++i)
      buildMI(MBB, MBBI, Opc).addReg(I8085::HL, RegState::Define).addReg(I8085::HL);
  };

  emitScratchAddr(MBB, MBBI, destReg, 0);
  for (int i = 0; i < 4; ++i) {
      buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::A, RegState::Define);
      if (delta != 0)
        bumpHL(deltaSteps, deltaOpc);
      if (i > 0)
        buildMI(MBB, MBBI, I8085::SBB_M);
      else
        buildMI(MBB, MBBI, I8085::SUB_M);
      if (delta != 0)
        bumpHL(deltaSteps, deltaBackOpc);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::A);
      if (i != 3)
        emitScratchAdvance(MBB, MBBI, 1);
  }

  MI.eraseFromParent();
  return true;
}


template <> bool I8085ExpandPseudo32::expand<I8085::ADDI_32>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned destReg = MI.getOperand(0).getReg();
  uint64_t immToAdd = MI.getOperand(2).getImm();

  auto values = splitImm32(immToAdd);

  emitScratchAddr(MBB, MBBI, destReg, 0);
  for (int i = 0; i < 4; ++i) {
      buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::A, RegState::Define);
      if (i > 0)
        buildMI(MBB, MBBI, I8085::ACI).addImm(values[i]);
      else
        buildMI(MBB, MBBI, I8085::ADI).addImm(values[i]);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::A);
      if (i != 3)
        emitScratchAdvance(MBB, MBBI, 1);
  }

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo32::expand<I8085::LOAD_32_WITH_ADDR>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  // If the result register is dead, this load is unnecessary (e.g. a dead
  // spill reload left over from register allocation). Expanding it would
  // generate code that clobbers A and HL (via implicit-def), which can
  // destroy live values set by prior instructions in the same basic block.
  if (MI.getOperand(0).isDead()) {
    MI.eraseFromParent();
    return true;
  }

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned baseReg = MI.getOperand(1).getReg();
  int64_t offsetToLoad = MI.getOperand(2).getImm();

  // Adjust source offset for HL preservation (PUSH H shifts SP by -2).
  if (HLSaveBias > 0 && baseReg == I8085::SP)
    offsetToLoad += HLSaveBias;

  // Check if B, C, D, E are all dead after this instruction.
  // If so, batch-load all 4 bytes into registers via one LXI+DAD + INX chain,
  // then batch-store to scratch via one LXI+DAD + INX chain.
  auto AfterMI = std::next(MBBI);
  bool CanBatch = true;
  for (MCRegister Reg : {I8085::B, I8085::C, I8085::D, I8085::E}) {
    if (MBB.computeRegisterLiveness(TRI, Reg, AfterMI, 20) !=
        MachineBasicBlock::LQR_Dead) {
      CanBatch = false;
      break;
    }
  }

  if (CanBatch) {
    // Phase 1: Load 4 bytes from [baseReg+offset] into B/C/D/E
    buildMI(MBB, MBBI, I8085::LXI).addReg(I8085::HL, RegState::Define).addImm(offsetToLoad);
    buildMI(MBB, MBBI, I8085::DAD).addReg(baseReg);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::B, RegState::Define);
    emitScratchAdvance(MBB, MBBI, 1);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::C, RegState::Define);
    emitScratchAdvance(MBB, MBBI, 1);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::D, RegState::Define);
    emitScratchAdvance(MBB, MBBI, 1);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::E, RegState::Define);

    // Phase 2: Store B/C/D/E to scratch slot, unless we can forward.
    if (!tryForwardBCDE(MBB, MBBI, destReg)) {
      emitScratchAddr(MBB, MBBI, destReg, 0);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::B);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::C);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::D);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::E);
    }
  } else {
    // Fallback: original per-byte approach with full address recomputation.
    // This path uses A as a temp register.  Since LOAD_32_WITH_ADDR no longer
    // declares Defs=[A], we must preserve A here if it is live.
    //
    // IMPORTANT: When PreserveA is true, we insert PUSH PSW which shifts SP
    // by -2. Both the source address (offsetToLoad) AND the scratch store
    // destination must be adjusted by +2 to compensate for the shifted SP.
    bool PreserveA =
        MBB.computeRegisterLiveness(TRI, I8085::A, MBBI, 20) !=
        MachineBasicBlock::LQR_Dead;
    int SpBias = 0;
    if (PreserveA) {
      // PUSH PSW to save A (and flags).
      buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::PSW);
      if (baseReg == I8085::SP)
        offsetToLoad += 2;
      SpBias = 2;  // All SP-relative addresses need +2 while PUSH is active
    }
    for(int i=0;i<4;i++){
      buildMI(MBB, MBBI, I8085::LXI).addReg(I8085::HL,RegState::Define).addImm(offsetToLoad+i);
      buildMI(MBB, MBBI, I8085::DAD).addReg(baseReg);
      buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::A,RegState::Define);
      // Use biased scratch store to account for PUSH PSW SP shift.
      emitScratchStore(MBB, MBBI, destReg, i, I8085::A, SpBias);
    }
    if (PreserveA)
      buildMI(MBB, MBBI, I8085::POP).addReg(I8085::PSW, RegState::Define);
  }

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo32::expand<I8085::LOAD_32>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  // Dead load: result unused, skip expansion to avoid clobbering A/HL.
  if (MI.getOperand(0).isDead()) {
    MI.eraseFromParent();
    return true;
  }

  unsigned destReg = MI.getOperand(0).getReg();
  uint64_t immToLoad = MI.getOperand(1).getImm();

  auto values = splitImm32(immToLoad);
  
  emitScratchAddr(MBB, MBBI, destReg, 0);
  for (int i = 0; i < 4; ++i) {
      buildMI(MBB, MBBI, I8085::MVI_M).addImm(values[i]);
      if (i != 3)
        emitScratchAdvance(MBB, MBBI, 1);
  }

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo32::expand<I8085::MVI_32>(Block &MBB, BlockIt MBBI) {
  return expand<I8085::LOAD_32>(MBB, MBBI);
}


template <> bool I8085ExpandPseudo32::expand<I8085::JMP_32_IF_NOT_EQUAL>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  MachineFunction *MF = MBB.getParent();
  const DebugLoc &DL = MI.getDebugLoc();

  unsigned operandOne = MI.getOperand(0).getReg();
  unsigned operandTwo = MI.getOperand(1).getReg();
  MachineBasicBlock *TargetMBB = MI.getOperand(2).getMBB();

  // Determine which bytes can be skipped (zero in both operands).
  // First try a backward walk from the JMP for any surviving definition
  // of each GR32 operand; fall back to the pre-scan map (which captured
  // masks before earlier pseudos in this MBB were expanded and erased).
  auto findKnownZeros = [&](unsigned Reg) -> unsigned {
    for (auto I = std::prev(MBBI.getReverse()), E = MBB.rend(); I != E; ++I) {
      if (I->getNumOperands() < 1 || !I->getOperand(0).isReg())
        continue;
      if (!I->getOperand(0).isDef())
        continue;
      if (I->getOperand(0).getReg() != Reg)
        continue;
      return computeKnownZeroMask(*I);
    }
    auto it = KnownZeroBytes.find(Reg);
    if (it != KnownZeroBytes.end())
      return it->second;
    return 0;
  };

  unsigned zeroMask1 = findKnownZeros(operandOne);
  unsigned zeroMask2 = findKnownZeros(operandTwo);
  unsigned skipMask = zeroMask1 & zeroMask2;

  LLVM_DEBUG(if (skipMask) dbgs() << "JMP_32_IF_NOT_EQUAL: skipMask=0x"
                                   << Twine::utohexstr(skipMask) << "\n");

  SmallVector<int, 4> compareBytes;
  for (int i = 0; i < 4; ++i)
    if (!(skipMask & (1 << i)))
      compareBytes.push_back(i);

  // If all bytes are known zero in both operands, they're always equal.
  if (compareBytes.empty()) {
    MI.eraseFromParent();
    return true;
  }

  LivePhysRegs LiveRegs(*TRI);
  LiveRegs.addLiveOuts(MBB);
  for (auto I = MBB.rbegin(), E = MBBI.getReverse(); I != E; ++I)
    LiveRegs.stepBackward(*I);
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || MO.isDef())
      continue;
    Register Reg = MO.getReg();
    if (Reg.isPhysical())
      LiveRegs.addReg(Reg);
  }

  const BasicBlock *LLVMBB = MBB.getBasicBlock();
  int numCmpMBBs = compareBytes.size() - 1;
  SmallVector<MachineBasicBlock *, 3> CmpMBBs;
  for (int i = 0; i < numCmpMBBs; ++i)
    CmpMBBs.push_back(MF->CreateMachineBasicBlock(LLVMBB));

  // TailMBB receives the "equal" continuation code spliced from MBB.
  // No DiffMBB — comparison blocks jump directly to TargetMBB.
  //
  // CRITICAL: Every comparison block has BOTH explicit branches
  // (JNZ TargetMBB + JMP next), ensuring analyzeBranch returns both
  // TBB and FBB. This prevents block-placement from incorrectly
  // inverting branch conditions when rearranging blocks.
  MachineBasicBlock *TailMBB = MF->CreateMachineBasicBlock(LLVMBB);

  auto InsertPos = std::next(MBB.getIterator());
  for (MachineBasicBlock *CmpMBB : CmpMBBs)
    MF->insert(InsertPos, CmpMBB);
  MF->insert(InsertPos, TailMBB);
  MF->RenumberBlocks();

  // Splice continuation code from MBB into TailMBB.
  TailMBB->splice(TailMBB->begin(), &MBB, std::next(MBBI), MBB.end());
  TailMBB->transferSuccessorsAndUpdatePHIs(&MBB);
  if (TailMBB->isSuccessor(TargetMBB)) {
    TailMBB->removeSuccessor(TargetMBB);
    TargetMBB->replacePhiUsesWith(TailMBB, &MBB);
  }

  // Wire up successors for MBB.
  MBB.addSuccessor(numCmpMBBs > 0 ? CmpMBBs[0] : TailMBB);
  MBB.addSuccessor(TargetMBB);

  // Wire up successors for CmpMBBs.
  for (int i = 0; i < numCmpMBBs; ++i) {
    MachineBasicBlock *NextMBB = (i == numCmpMBBs - 1) ? TailMBB : CmpMBBs[i + 1];
    CmpMBBs[i]->addSuccessor(NextMBB);
    CmpMBBs[i]->addSuccessor(TargetMBB);
  }

  // First comparison byte in MBB — always has both JNZ + JMP.
  int firstByte = compareBytes[0];
  emitScratchLoad(MBB, MBBI, operandTwo, firstByte, I8085::A);
  emitScratchAddr(MBB, MBBI, operandOne, firstByte);
  buildMI(MBB, MBBI, I8085::CMP_M);
  buildMI(MBB, MBBI, I8085::JNZ).addMBB(TargetMBB);
  buildMI(MBB, MBBI, I8085::JMP).addMBB(numCmpMBBs > 0 ? CmpMBBs[0] : TailMBB);

  // Remaining comparison bytes in CmpMBBs — all have both JNZ + JMP.
  for (int i = 1; i < (int)compareBytes.size(); ++i) {
    int byteIdx = compareBytes[i];
    MachineBasicBlock *CurMBB = CmpMBBs[i - 1];
    MachineBasicBlock *NextMBB = (i == (int)compareBytes.size() - 1) ? TailMBB : CmpMBBs[i];
    emitScratchLoad(*CurMBB, DL, operandTwo, byteIdx, I8085::A);
    emitScratchAddr(*CurMBB, DL, operandOne, byteIdx);
    BuildMI(CurMBB, DL, TII->get(I8085::CMP_M));
    BuildMI(CurMBB, DL, TII->get(I8085::JNZ)).addMBB(TargetMBB);
    BuildMI(CurMBB, DL, TII->get(I8085::JMP)).addMBB(NextMBB);
  }

  // If TailMBB has no terminator but has exactly one successor, add a JMP.
  if (TailMBB->succ_size() == 1) {
    auto Last = TailMBB->getLastNonDebugInstr();
    if (Last == TailMBB->end() || !Last->isTerminator())
      BuildMI(TailMBB, DL, TII->get(I8085::JMP))
          .addMBB(*TailMBB->succ_begin());
  }

  for (MachineBasicBlock *CmpMBB : CmpMBBs)
    addLiveIns(*CmpMBB, LiveRegs);
  addLiveIns(*TailMBB, LiveRegs);

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo32::expand<I8085::JMP_32_IF_ULT>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  MachineFunction *MF = MBB.getParent();
  const DebugLoc &DL = MI.getDebugLoc();

  unsigned operandOne = MI.getOperand(0).getReg();
  unsigned operandTwo = MI.getOperand(1).getReg();
  MachineBasicBlock *TargetMBB = MI.getOperand(2).getMBB();

  LivePhysRegs LiveRegs(*TRI);
  LiveRegs.addLiveOuts(MBB);
  for (auto I = MBB.rbegin(), E = MBBI.getReverse(); I != E; ++I)
    LiveRegs.stepBackward(*I);
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || MO.isDef())
      continue;
    Register Reg = MO.getReg();
    if (Reg.isPhysical())
      LiveRegs.addReg(Reg);
  }

  const BasicBlock *LLVMBB = MBB.getBasicBlock();
  SmallVector<MachineBasicBlock *, 3> CmpMBBs;
  for (int i = 0; i < 3; ++i)
    CmpMBBs.push_back(MF->CreateMachineBasicBlock(LLVMBB));
  MachineBasicBlock *TailMBB = MF->CreateMachineBasicBlock(LLVMBB);
  MachineBasicBlock *LtMBB = MF->CreateMachineBasicBlock(LLVMBB);

  auto InsertPos = std::next(MBB.getIterator());
  for (MachineBasicBlock *CmpMBB : CmpMBBs)
    MF->insert(InsertPos, CmpMBB);
  MF->insert(InsertPos, TailMBB);
  MF->insert(InsertPos, LtMBB);
  // Renumber ALL blocks to avoid conflicts with existing block numbers.
  MF->RenumberBlocks();

  TailMBB->splice(TailMBB->begin(), &MBB, std::next(MBBI), MBB.end());
  TailMBB->transferSuccessorsAndUpdatePHIs(&MBB);
  if (TailMBB->isSuccessor(TargetMBB)) {
    TailMBB->removeSuccessor(TargetMBB);
    TargetMBB->replacePhiUsesWith(TailMBB, LtMBB);
  }

  MBB.addSuccessor(CmpMBBs[0]);
  MBB.addSuccessor(LtMBB);
  MBB.addSuccessor(TailMBB);

  CmpMBBs[0]->addSuccessor(CmpMBBs[1]);
  CmpMBBs[0]->addSuccessor(LtMBB);
  CmpMBBs[0]->addSuccessor(TailMBB);

  CmpMBBs[1]->addSuccessor(CmpMBBs[2]);
  CmpMBBs[1]->addSuccessor(LtMBB);
  CmpMBBs[1]->addSuccessor(TailMBB);

  CmpMBBs[2]->addSuccessor(LtMBB);
  CmpMBBs[2]->addSuccessor(TailMBB);

  LtMBB->addSuccessor(TargetMBB);

  addLiveIns(*CmpMBBs[0], LiveRegs);
  addLiveIns(*CmpMBBs[1], LiveRegs);
  addLiveIns(*CmpMBBs[2], LiveRegs);
  addLiveIns(*TailMBB, LiveRegs);
  addLiveIns(*LtMBB, LiveRegs);

  // Compare byte3 -> byte2 -> byte1 -> byte0.
  emitScratchLoad(MBB, MBBI, operandOne, 3, I8085::A);
  emitScratchAddr(MBB, MBBI, operandTwo, 3);
  buildMI(MBB, MBBI, I8085::CMP_M);
  buildMI(MBB, MBBI, I8085::JC).addMBB(LtMBB);
  buildMI(MBB, MBBI, I8085::JNZ).addMBB(TailMBB);
  buildMI(MBB, MBBI, I8085::JMP).addMBB(CmpMBBs[0]);

  for (int i = 0; i < 3; ++i) {
    MachineBasicBlock *CurMBB = CmpMBBs[i];
    int ByteIndex = 2 - i;
    emitScratchLoad(*CurMBB, DL, operandOne, ByteIndex, I8085::A);
    emitScratchAddr(*CurMBB, DL, operandTwo, ByteIndex);
    BuildMI(CurMBB, DL, TII->get(I8085::CMP_M));
    BuildMI(CurMBB, DL, TII->get(I8085::JC)).addMBB(LtMBB);
    BuildMI(CurMBB, DL, TII->get(I8085::JNZ)).addMBB(TailMBB);
    MachineBasicBlock *NextMBB = (i == 2) ? TailMBB : CmpMBBs[i + 1];
    BuildMI(CurMBB, DL, TII->get(I8085::JMP)).addMBB(NextMBB);
  }

  BuildMI(LtMBB, DL, TII->get(I8085::JMP)).addMBB(TargetMBB);

  if (TailMBB->succ_size() == 1) {
    auto Last = TailMBB->getLastNonDebugInstr();
    if (Last == TailMBB->end() || !Last->isTerminator())
      BuildMI(TailMBB, DL, TII->get(I8085::JMP))
          .addMBB(*TailMBB->succ_begin());
  }

  MI.eraseFromParent();
  return true;
}


template <> bool I8085ExpandPseudo32::expand<I8085::JMP_32_IF_SAME_SIGN>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned operandOne = MI.getOperand(0).getReg();
  unsigned operandTwo = MI.getOperand(1).getReg();
  
  emitScratchLoad(MBB, MBBI, operandTwo, 3, I8085::A);
  emitScratchAddr(MBB, MBBI, operandOne, 3);
  buildMI(MBB, MBBI, I8085::XRA_M);
  buildMI(MBB, MBBI, I8085::ANI).addImm(128);  
  buildMI(MBB, MBBI, I8085::JZ).addMBB(MI.getOperand(2).getMBB());
        
  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo32::expand<I8085::JMP_32_IF_POSITIVE>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned operandOne = MI.getOperand(0).getReg();
  
  emitScratchLoad(MBB, MBBI, operandOne, 3, I8085::A);
  buildMI(MBB, MBBI, I8085::ANI).addImm(128);  
  buildMI(MBB, MBBI, I8085::JZ).addMBB(MI.getOperand(1).getMBB());
        
  MI.eraseFromParent();
  return true;
}


template <> bool I8085ExpandPseudo32::expand<I8085::STORE_32_AT_OFFSET_WITH_SP>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  unsigned srcReg = MI.getOperand(0).getReg();
  unsigned offsetToStore = MI.getOperand(1).getImm();

  // Always SP-relative: adjust for HL preservation.
  offsetToStore += HLSaveBias;

  // Check if B, C, D, E are all dead after this instruction.
  // If so, batch-load from scratch into B/C/D/E, then batch-store to dest.
  auto AfterMI = std::next(MBBI);
  bool CanBatch = true;
  for (MCRegister Reg : {I8085::B, I8085::C, I8085::D, I8085::E}) {
    if (MBB.computeRegisterLiveness(TRI, Reg, AfterMI, 20) !=
        MachineBasicBlock::LQR_Dead) {
      CanBatch = false;
      break;
    }
  }

  if (CanBatch) {
    // Phase 1: Load 4 bytes from scratch into B/C/D/E
    emitScratchAddr(MBB, MBBI, srcReg, 0);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::B, RegState::Define);
    emitScratchAdvance(MBB, MBBI, 1);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::C, RegState::Define);
    emitScratchAdvance(MBB, MBBI, 1);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::D, RegState::Define);
    emitScratchAdvance(MBB, MBBI, 1);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::E, RegState::Define);

    // Phase 2: Store B/C/D/E to [SP+offset] via one LXI+DAD + INX chain
    buildMI(MBB, MBBI, I8085::LXI).addReg(I8085::HL, RegState::Define).addImm(offsetToStore);
    buildMI(MBB, MBBI, I8085::DAD).addReg(I8085::SP);
    buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::B);
    emitScratchAdvance(MBB, MBBI, 1);
    buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::C);
    emitScratchAdvance(MBB, MBBI, 1);
    buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::D);
    emitScratchAdvance(MBB, MBBI, 1);
    buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::E);
  } else {
    // Fallback: original per-byte approach
    for(int i=0;i<4;i++){
      emitScratchLoad(MBB, MBBI, srcReg, i, I8085::A);
      buildMI(MBB, MBBI, I8085::LXI).addReg(I8085::HL,RegState::Define).addImm(offsetToStore+i);
      buildMI(MBB, MBBI, I8085::DAD).addReg(I8085::SP);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::A);
    }
  }

  MI.eraseFromParent();
  return true;
}


template <> bool I8085ExpandPseudo32::expand<I8085::LOAD_32_OFFSET_WITH_SP>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  // Dead load: result unused, skip expansion to avoid clobbering A/HL.
  if (MI.getOperand(0).isDead()) {
    MI.eraseFromParent();
    return true;
  }

  unsigned destReg = MI.getOperand(0).getReg();
  uint16_t offsetToLoad = MI.getOperand(1).getImm();

  // Always SP-relative: adjust for HL preservation.
  offsetToLoad += HLSaveBias;

  // Check if B, C, D, E are all dead after this instruction.
  // If so, batch-load all 4 bytes into registers via one LXI+DAD + INX chain,
  // then batch-store to scratch via one LXI+DAD + INX chain.
  auto AfterMI = std::next(MBBI);
  bool CanBatch = true;
  for (MCRegister Reg : {I8085::B, I8085::C, I8085::D, I8085::E}) {
    if (MBB.computeRegisterLiveness(TRI, Reg, AfterMI, 20) !=
        MachineBasicBlock::LQR_Dead) {
      CanBatch = false;
      break;
    }
  }

  if (CanBatch) {
    // Phase 1: Load 4 bytes from [SP+offset] into B/C/D/E
    buildMI(MBB, MBBI, I8085::LXI).addReg(I8085::HL, RegState::Define).addImm(offsetToLoad);
    buildMI(MBB, MBBI, I8085::DAD).addReg(I8085::SP);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::B, RegState::Define);
    emitScratchAdvance(MBB, MBBI, 1);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::C, RegState::Define);
    emitScratchAdvance(MBB, MBBI, 1);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::D, RegState::Define);
    emitScratchAdvance(MBB, MBBI, 1);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::E, RegState::Define);

    // Phase 2: Store B/C/D/E to scratch slot, unless we can forward.
    if (!tryForwardBCDE(MBB, MBBI, destReg)) {
      emitScratchAddr(MBB, MBBI, destReg, 0);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::B);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::C);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::D);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::E);
    }
  } else {
    // Fallback: original per-byte approach.
    // This path uses A as a temp.  Preserve A if live.
    bool PreserveA =
        MBB.computeRegisterLiveness(TRI, I8085::A, MBBI, 20) !=
        MachineBasicBlock::LQR_Dead;
    if (PreserveA) {
      buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::PSW);
      offsetToLoad += 2;
    }
    // When PreserveA is true, SP is biased by -2 (PUSH PSW), so scratch
    // stores via emitScratchAddr (which uses DAD SP) would write 2 bytes
    // too low.  Add a +2 bias to compensate.
    int SpBias = PreserveA ? 2 : 0;
    for(int i=0;i<4;i++){
      buildMI(MBB, MBBI, I8085::LXI).addReg(I8085::HL,RegState::Define).addImm(offsetToLoad+i);
      buildMI(MBB, MBBI, I8085::DAD).addReg(I8085::SP);
      buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::A,RegState::Define);
      emitScratchStore(MBB, MBBI, destReg, i, I8085::A, SpBias);
    }
    if (PreserveA)
      buildMI(MBB, MBBI, I8085::POP).addReg(I8085::PSW, RegState::Define);
  }

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo32::expand<I8085::LOAD_32_WITH_IMM_ADDR>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  // Dead load: result unused, skip expansion to avoid clobbering A/HL.
  if (MI.getOperand(0).isDead()) {
    MI.eraseFromParent();
    return true;
  }

  unsigned destReg = MI.getOperand(0).getReg();

  const MachineOperand &AddrMO = MI.getOperand(1);

  // Check if B, C, D, E are all dead after this instruction.
  // If so, batch-load all 4 bytes into registers via one LXI + INX chain,
  // then batch-store to scratch via one LXI+DAD + INX chain.
  auto AfterMI = std::next(MBBI);
  bool CanBatch = true;
  for (MCRegister Reg : {I8085::B, I8085::C, I8085::D, I8085::E}) {
    if (MBB.computeRegisterLiveness(TRI, Reg, AfterMI, 20) !=
        MachineBasicBlock::LQR_Dead) {
      CanBatch = false;
      break;
    }
  }

  if (CanBatch) {
    // Phase 1: Load 4 bytes from [immAddr] into B/C/D/E
    {
      MachineInstrBuilder Addr =
          buildMI(MBB, MBBI, I8085::LXI).addReg(I8085::HL, RegState::Define);
      addAddrOperand(Addr, AddrMO, 0);
    }
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::B, RegState::Define);
    emitScratchAdvance(MBB, MBBI, 1);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::C, RegState::Define);
    emitScratchAdvance(MBB, MBBI, 1);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::D, RegState::Define);
    emitScratchAdvance(MBB, MBBI, 1);
    buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::E, RegState::Define);

    // Phase 2: Store B/C/D/E to scratch slot, unless we can forward.
    if (!tryForwardBCDE(MBB, MBBI, destReg)) {
      emitScratchAddr(MBB, MBBI, destReg, 0);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::B);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::C);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::D);
      emitScratchAdvance(MBB, MBBI, 1);
      buildMI(MBB, MBBI, I8085::MOV_M).addReg(I8085::E);
    }
  } else {
    // Fallback: original per-byte approach.
    // This path uses A as a temp.  Preserve A if live.
    bool PreserveA =
        MBB.computeRegisterLiveness(TRI, I8085::A, MBBI, 20) !=
        MachineBasicBlock::LQR_Dead;
    if (PreserveA)
      buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::PSW);
    // When PreserveA is true, SP is biased by -2 (PUSH PSW), so scratch
    // stores via emitScratchAddr (which uses DAD SP) would write 2 bytes
    // too low.  Add a +2 bias to compensate.
    int SpBias = PreserveA ? 2 : 0;
    for(int i=0;i<4;i++){
      MachineInstrBuilder Addr =
          buildMI(MBB, MBBI, I8085::LXI).addReg(I8085::HL, RegState::Define);
      addAddrOperand(Addr, AddrMO, i);
      buildMI(MBB, MBBI, I8085::MOV_FROM_M).addReg(I8085::A,RegState::Define);
      emitScratchStore(MBB, MBBI, destReg, i, I8085::A, SpBias);
    }
    if (PreserveA)
      buildMI(MBB, MBBI, I8085::POP).addReg(I8085::PSW, RegState::Define);
  }

  MI.eraseFromParent();
  return true;
}

template <> bool I8085ExpandPseudo32::expand<I8085::LOAD_32_ADDR_CONTENT>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  // Dead load: result unused, skip expansion to avoid clobbering A/HL/BC.
  if (MI.getOperand(0).isDead()) {
    MI.eraseFromParent();
    return true;
  }

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned srcReg = MI.getOperand(1).getReg();

  bool addrIsSP = (srcReg == I8085::SP);
  unsigned opOneLow = 0, opOneHigh = 0;
  bool haveAddr = true;
  bool srcIsHL = false;
  if (!addrIsSP) {
    if (srcReg == I8085::BC) {
      opOneLow = I8085::C;
      opOneHigh = I8085::B;
    } else if (srcReg == I8085::DE) {
      opOneLow = I8085::E;
      opOneHigh = I8085::D;
    } else if (srcReg == I8085::HL) {
      // When source is HL, we must save it to BC before the loop since
      // emitScratchStore clobbers HL. We'll use BC to hold the address.
      srcIsHL = true;
      opOneLow = I8085::C;
      opOneHigh = I8085::B;
    } else {
      haveAddr = false;
    }
  }

  if (!haveAddr)
    return false;

  // If source address is in HL, copy it to BC first. HL will be clobbered
  // by scratch store operations, but BC will preserve the address.
  if (srcIsHL) {
    buildMI(MBB, MBBI, I8085::MOV).addReg(I8085::B, RegState::Define).addReg(I8085::H);
    buildMI(MBB, MBBI, I8085::MOV).addReg(I8085::C, RegState::Define).addReg(I8085::L);
  }

  for(int i=0;i<4;i++){
      if (addrIsSP) {
        buildMI(MBB, MBBI, I8085::LXI).addReg(I8085::HL,RegState::Define).addImm(HLSaveBias);
        buildMI(MBB, MBBI, I8085::DAD).addReg(I8085::SP);
      } else {
        buildMI(MBB, MBBI,  I8085::MOV).addReg(I8085::H, RegState::Define).addReg(opOneHigh);
        buildMI(MBB, MBBI,  I8085::MOV).addReg(I8085::L, RegState::Define).addReg(opOneLow);
      }

      for (int j = 0; j < i; ++j)
        buildMI(MBB, MBBI, I8085::INX).addReg(I8085::HL,RegState::Define).addReg(I8085::HL);

      buildMI(MBB, MBBI,  I8085::MOV_FROM_M).addReg(I8085::A,RegState::Define);

      emitScratchStore(MBB, MBBI, destReg, i, I8085::A);
  }

  MI.eraseFromParent();
  return true;
}

bool I8085ExpandPseudo32::expandMI(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  int Opcode = MBBI->getOpcode();

  // Check if HL needs to be preserved across this expansion.
  // The register allocator may place a live HL value across a GR32 pseudo
  // that clobbers HL (implicit-def $hl).  We detect this by checking if the
  // implicit-def of $hl is NOT dead.  If so, we wrap the expansion in
  // PUSH H / POP H and adjust all SP-relative offsets via HLSaveBias.
  //
  // Additionally, we perform a defensive liveness check: the Machine LICM
  // pass can hoist instructions to loop preheaders, preserving a stale
  // "dead" flag on implicit-def $hl even though HL becomes live-out in the
  // new position.  To catch this, we recompute HL liveness from block
  // live-outs when the dead flag is set but HL appears live.
  //
  // Skip branch/terminator pseudos — they may split blocks and HL is
  // unlikely to be live across them.
  bool NeedHLSave = false;
  if (!MI.isBranch() && !MI.isTerminator()) {
    bool HasHLImplicitDef = false;
    bool HLImplicitDefIsDead = false;
    for (const MachineOperand &MO : MI.implicit_operands()) {
      if (MO.isReg() && MO.isDef() && MO.getReg() == I8085::HL) {
        HasHLImplicitDef = true;
        HLImplicitDefIsDead = MO.isDead();
        if (!HLImplicitDefIsDead) {
          NeedHLSave = true;
        }
        break;
      }
    }
    // Defensive check: if the implicit-def is marked dead, verify that HL
    // is truly dead by recomputing liveness from block live-outs.  Machine
    // LICM can hoist instructions and leave stale dead flags.
    if (HasHLImplicitDef && HLImplicitDefIsDead && !NeedHLSave) {
      LivePhysRegs LiveRegs(*TRI);
      LiveRegs.addLiveOuts(MBB);
      for (auto I = MBB.rbegin(), E = MBBI.getReverse(); I != E; ++I)
        LiveRegs.stepBackward(*I);
      if (LiveRegs.contains(I8085::HL) ||
          LiveRegs.contains(I8085::H) ||
          LiveRegs.contains(I8085::L)) {
        NeedHLSave = true;
        LLVM_DEBUG(dbgs() << "HL-save: stale dead flag on implicit-def $hl, "
                          << "HL is actually live — wrapping with PUSH/POP\n");
      }
    }
  }

  if (NeedHLSave && !hasAvailableHLValue(MBB, MBBI))
    NeedHLSave = false;

  // Check if the flags (SREG) must be preserved across this expansion.
  //
  // The GR32 pseudos that do NOT declare Defs=[SREG] (MOV_32, the loads and
  // stores, MVI_32, the extends/truncates, PACK*, BSWAP32) are treated by the
  // scheduler and phi-elimination as flag-transparent, so one can legally be
  // placed between a flag-setting compare and a flag-consuming branch -- the
  // classic case is the loop-latch phi copy `$ibx = MOV_32 $iax` that lands
  // between the `SBB` loop-exit test and the `JC` back-edge.  But their
  // expansion addresses the scratch area with `LXI H,off ; DAD SP`, and DAD
  // clobbers carry.  So when SREG is live across such a pseudo we must save and
  // restore it, exactly like HL above.  (Pseudos that DO declare Defs=[SREG]
  // are excluded: the scheduler already knows they destroy flags, and wrapping
  // them would be wrong.)
  bool NeedPSWSave = false;
  if (!MI.isBranch() && !MI.isTerminator()) {
    bool DefinesSREG = false;
    for (const MachineOperand &MO : MI.operands())
      if (MO.isReg() && MO.isDef() && MO.getReg() == I8085::SREG) {
        DefinesSREG = true;
        break;
      }
    if (!DefinesSREG) {
      // SREG is live across this pseudo iff a following instruction reads it
      // before any instruction redefines it.  Scan forward to the first SREG
      // event -- normally one or two instructions (the consuming branch, or an
      // ALU op that overwrites the flags), so this stays O(1) per pseudo rather
      // than the O(block) backward walk it replaced (which was quadratic over a
      // block full of GR32 pseudos and made compile time blow up).  A short cap
      // keeps it linear even in the pathological long-run case: if unresolved,
      // fall back to a single live-out query and, failing that, save
      // conservatively (a needless PUSH/POP PSW is only a size cost).
      const unsigned Cap = 64;
      unsigned Steps = 0;
      bool Resolved = false;
      for (auto I = std::next(MBBI); I != MBB.end(); ++I) {
        if (I->readsRegister(I8085::SREG, TRI)) {
          NeedPSWSave = true;
          Resolved = true;
          break;
        }
        if (I->modifiesRegister(I8085::SREG, TRI)) {
          NeedPSWSave = false;
          Resolved = true;
          break;
        }
        if (++Steps >= Cap) {
          NeedPSWSave = true; // undecided within the cap: be conservative
          Resolved = true;
          break;
        }
      }
      if (!Resolved) {
        // Ran off the block end with no local SREG read or def: SREG is live
        // across this pseudo iff it is live-out of the block.
        LivePhysRegs LiveRegs(*TRI);
        LiveRegs.addLiveOuts(MBB);
        NeedPSWSave = LiveRegs.contains(I8085::SREG);
      }
    }
  }

  BlockIt AfterPseudo = std::next(MBBI);
  DebugLoc DL = MI.getDebugLoc();

  // Emit the save prologue.  PSW is pushed first, HL second, so the LIFO
  // restore below pops HL first and PSW last.  All SP-relative addressing in
  // the expansion compensates for the pushed bytes through HLSaveBias.
  if (NeedPSWSave)
    buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::PSW);
  if (NeedHLSave)
    buildMI(MBB, MBBI, I8085::PUSH).addReg(I8085::HL);
  HLSaveBias = (NeedPSWSave ? 2 : 0) + (NeedHLSave ? 2 : 0);

#define EXPAND(Op)                                                             \
  case Op:                                                                     \
    result = expand<Op>(MBB, MI);                                              \
    break

  bool result = false;
  switch (Opcode) {
    EXPAND(I8085::XORI_32);
    EXPAND(I8085::ORI_32);
    EXPAND(I8085::ANDI_32);
    EXPAND(I8085::XOR_32);
    EXPAND(I8085::OR_32);
    EXPAND(I8085::AND_32);
    EXPAND(I8085::RL_32);
    EXPAND(I8085::RR_32);
    EXPAND(I8085::ASR_32);
    EXPAND(I8085::SHL_32_BY_8);
    EXPAND(I8085::SHL_32_BY_16);
    EXPAND(I8085::SHL_32_BY_24);
    EXPAND(I8085::SRL_32_BY_8);
    EXPAND(I8085::SRL_32_BY_16);
    EXPAND(I8085::SRL_32_BY_24);
    EXPAND(I8085::ASR_32_BY_8);
    EXPAND(I8085::ASR_32_BY_16);
    EXPAND(I8085::SEXT32_INREG_16);
    EXPAND(I8085::SEXT32_INREG_8);
    EXPAND(I8085::TRUNC32TO16);
    EXPAND(I8085::TRUNC32TO16_HI);
    EXPAND(I8085::SEXT16TO32);
    EXPAND(I8085::AEXT16TO32);
    EXPAND(I8085::ZEXT16TO32);
    EXPAND(I8085::TRUNC32TO8);
    EXPAND(I8085::SEXT8TO32);
    EXPAND(I8085::AEXT8TO32);
    EXPAND(I8085::ZEXT8TO32);
    EXPAND(I8085::LOAD_32_OFFSET_WITH_SP);
    EXPAND(I8085::STORE_32_AT_OFFSET_WITH_SP);
    EXPAND(I8085::STORE_32_ADDR_CONTENT);
    EXPAND(I8085::JMP_32_IF_POSITIVE);
    EXPAND(I8085::JMP_32_IF_SAME_SIGN);
    EXPAND(I8085::JMP_32_IF_NOT_EQUAL);
    EXPAND(I8085::JMP_32_IF_ULT);
    EXPAND(I8085::MOV_32);
    EXPAND(I8085::PACK_16_TO_32);
    EXPAND(I8085::PACK_BCDE_TO_32);
    EXPAND(I8085::BSWAP32);
    EXPAND(I8085::SUBI_32);
    EXPAND(I8085::SUB_32);
    EXPAND(I8085::ADD_32);
    EXPAND(I8085::ADDI_32);
    EXPAND(I8085::LOAD_32);
    EXPAND(I8085::STORE_32);
    EXPAND(I8085::LOAD_32_WITH_ADDR);
    EXPAND(I8085::LOAD_32_WITH_IMM_ADDR);
    EXPAND(I8085::LOAD_32_ADDR_CONTENT);
    EXPAND(I8085::MVI_32);
    default: break;
  }
#undef EXPAND

  if (NeedPSWSave || NeedHLSave) {
    if (result) {
      // LIFO restore: HL was pushed last so it pops first; PSW pops last.
      // Both are inserted before AfterPseudo, so inserting HL then PSW yields
      // the order POP H, POP PSW.
      if (NeedHLSave)
        BuildMI(MBB, AfterPseudo, DL, TII->get(I8085::POP))
            .addReg(I8085::HL, RegState::Define);
      if (NeedPSWSave)
        BuildMI(MBB, AfterPseudo, DL, TII->get(I8085::POP))
            .addReg(I8085::PSW, RegState::Define);
    } else {
      // Expansion didn't happen; remove the pushes we inserted.  They sit
      // immediately before MBBI in the order PUSH PSW, PUSH H, so erasing the
      // predecessor twice removes H then PSW.
      if (NeedHLSave)
        std::prev(MBBI)->eraseFromParent();
      if (NeedPSWSave)
        std::prev(MBBI)->eraseFromParent();
    }
    HLSaveBias = 0;
  }

  return result;
}

} // end of anonymous namespace

INITIALIZE_PASS(I8085ExpandPseudo32, "i8085-expand-pseudo32", I8085_EXPAND_PSEUDO_32_NAME,
                false, false)
namespace llvm { 

FunctionPass *createI8085ExpandPseudo32Pass() {  return new I8085ExpandPseudo32();  }

} // end of namespace llvm
