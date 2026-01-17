//===-- TMS9900AsmBackend.cpp - TMS9900 Assembler Backend -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/TMS9900FixupKinds.h"
#include "MCTargetDesc/TMS9900MCTargetDesc.h"
#include "llvm/ADT/APInt.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

class TMS9900AsmBackend : public MCAsmBackend {
  uint8_t OSABI;

  uint64_t adjustFixupValue(const MCFixup &Fixup, uint64_t Value,
                            MCContext &Ctx) const;

public:
  TMS9900AsmBackend(const MCSubtargetInfo &STI, uint8_t OSABI)
      : MCAsmBackend(llvm::endianness::big), OSABI(OSABI) {}
  ~TMS9900AsmBackend() override = default;

  void applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                  const MCValue &Target, MutableArrayRef<char> Data,
                  uint64_t Value, bool IsResolved,
                  const MCSubtargetInfo *STI) const override;

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createTMS9900ELFObjectWriter(OSABI);
  }

  // Check if a fixup needs relaxation (branch out of range)
  bool fixupNeedsRelaxation(const MCFixup &Fixup, uint64_t Value,
                            const MCRelaxableFragment *DF,
                            const MCAsmLayout &Layout) const override {
    // Only relax 8-bit PC-relative branch fixups
    if (static_cast<unsigned>(Fixup.getKind()) !=
        static_cast<unsigned>(TMS9900::fixup_tms9900_pcrel_8))
      return false;

    // Convert to signed word offset
    int64_t Offset = Value;
    if (Offset & 1)
      return true;  // Misaligned, will fail anyway

    Offset >>= 1;   // Convert to word offset
    Offset -= 1;    // PC points to next instruction

    // Check if out of 8-bit signed range (-128 to +127 words)
    return Offset < -128 || Offset > 127;
  }

  bool fixupNeedsRelaxationAdvanced(const MCFixup &Fixup, bool Resolved,
                                    uint64_t Value,
                                    const MCRelaxableFragment *DF,
                                    const MCAsmLayout &Layout,
                                    const bool WasForced) const override {
    // Only relax when the offset is known to be out of range.
    if (!Resolved && !WasForced)
      return false;

    return fixupNeedsRelaxation(Fixup, Value, DF, Layout);
  }

  // Relax a branch instruction to a longer form
  void relaxInstruction(MCInst &Inst,
                        const MCSubtargetInfo &STI) const override {
    assert(Inst.getNumOperands() >= 1 && "Branch must have target operand");
    assert(Inst.getOpcode() == TMS9900::JMP &&
           "Only JMP can be relaxed (mayNeedRelaxation should filter others)");

    // Convert JMP to B @target (4 bytes instead of 2)
    MCInst Relaxed;
    Relaxed.setOpcode(TMS9900::B_sym);
    Relaxed.addOperand(Inst.getOperand(0));  // Copy target operand
    Inst = std::move(Relaxed);
  }

  // Check if instruction may need relaxation
  bool mayNeedRelaxation(const MCInst &Inst,
                         const MCSubtargetInfo &STI) const override {
    // Only unconditional JMP can be relaxed to B @target.
    // Conditional long branches are handled in codegen (branch relaxation +
    // tms9900-long-branch pass), so the MC layer leaves them alone.
    // The code generator should emit patterns like:
    //   Jcc label    ; short range conditional
    //   JMP target   ; this can be relaxed if out of range
    if (Inst.getOpcode() != TMS9900::JMP)
      return false;

    // Preserve JMP 0 (used for NOP) even with -mrelax-all.
    if (Inst.getNumOperands() > 0) {
      const MCOperand &Op = Inst.getOperand(0);
      if (Op.isImm() && Op.getImm() == 0)
        return false;
    }

    return true;
  }

  unsigned getNumFixupKinds() const override {
    return TMS9900::NumTargetFixupKinds;
  }

  const MCFixupKindInfo &getFixupKindInfo(MCFixupKind Kind) const override {
    const static MCFixupKindInfo Infos[TMS9900::NumTargetFixupKinds] = {
        // This table must be in the same order of enum in TMS9900FixupKinds.h.
        //
        // name                    offset bits flags
        {"fixup_tms9900_16",       0,     16,  0},
        {"fixup_tms9900_8",        0,     8,   0},
        {"fixup_tms9900_pcrel_8",  0,     8,   MCFixupKindInfo::FKF_IsPCRel},
        {"fixup_tms9900_pcrel_16", 0,     16,  MCFixupKindInfo::FKF_IsPCRel},
    };
    static_assert((std::size(Infos)) == TMS9900::NumTargetFixupKinds,
                  "Not all fixup kinds added to Infos array");

    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);

    return Infos[Kind - FirstTargetFixupKind];
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override;
};

uint64_t TMS9900AsmBackend::adjustFixupValue(const MCFixup &Fixup,
                                              uint64_t Value,
                                              MCContext &Ctx) const {
  unsigned Kind = Fixup.getKind();
  switch (Kind) {
  case TMS9900::fixup_tms9900_8: {
    int64_t SignedVal = static_cast<int64_t>(Value);
    if (SignedVal < -128 || SignedVal > 127)
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range");
    return static_cast<uint64_t>(SignedVal) & 0xFF;
  }
  case TMS9900::fixup_tms9900_pcrel_8: {
    // PC-relative 8-bit displacement for jump instructions
    // TMS9900 jumps: displacement is in words, signed, relative to next instr
    if (Value & 0x1)
      Ctx.reportError(Fixup.getLoc(), "fixup value must be 2-byte aligned");

    // Convert byte offset to word offset
    int16_t Offset = Value;
    Offset >>= 1;
    // PC points to next instruction, so subtract 1
    --Offset;

    // Check range: -128 to +127 words
    if (Offset < -128 || Offset > 127)
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range");

    // Mask to 8 bits
    return Offset & 0xFF;
  }
  default:
    return Value;
  }
}

void TMS9900AsmBackend::applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                                    const MCValue &Target,
                                    MutableArrayRef<char> Data,
                                    uint64_t Value, bool IsResolved,
                                    const MCSubtargetInfo *STI) const {
  Value = adjustFixupValue(Fixup, Value, Asm.getContext());
  if (!Value)
    return; // Doesn't change encoding.

  unsigned Offset = Fixup.getOffset();
  unsigned Kind = Fixup.getKind();

  // Handle TMS9900-specific fixups
  if (Kind == TMS9900::fixup_tms9900_pcrel_8 ||
      Kind == TMS9900::fixup_tms9900_8) {
    // 8-bit displacement in the LOW byte of a 16-bit instruction word
    // For big-endian, low byte is at offset+1
    assert(Offset + 2 <= Data.size() && "Invalid fixup offset!");
    Data[Offset + 1] = Value & 0xFF;
    return;
  }

  // For 16-bit fixups, write in big-endian order
  MCFixupKindInfo Info = getFixupKindInfo(static_cast<MCFixupKind>(Kind));
  unsigned NumBytes = alignTo(Info.TargetSize + Info.TargetOffset, 8) / 8;
  assert(Offset + NumBytes <= Data.size() && "Invalid fixup offset!");

  // TMS9900 is big-endian
  for (unsigned i = 0; i != NumBytes; ++i) {
    Data[Offset + i] |= uint8_t((Value >> ((NumBytes - 1 - i) * 8)) & 0xff);
  }
}

bool TMS9900AsmBackend::writeNopData(raw_ostream &OS, uint64_t Count,
                                      const MCSubtargetInfo *STI) const {
  if ((Count % 2) != 0)
    return false;

  // TMS9900 NOP is typically "JMP $+2" (jump to next instruction)
  // Encoding: 0x1000 (JMP with 0 displacement)
  uint64_t NopCount = Count / 2;
  while (NopCount--)
    OS.write("\x10\x00", 2);

  return true;
}

} // end anonymous namespace

MCAsmBackend *llvm::createTMS9900MCAsmBackend(const Target &T,
                                               const MCSubtargetInfo &STI,
                                               const MCRegisterInfo &MRI,
                                               const MCTargetOptions &Options) {
  return new TMS9900AsmBackend(STI, ELF::ELFOSABI_STANDALONE);
}
