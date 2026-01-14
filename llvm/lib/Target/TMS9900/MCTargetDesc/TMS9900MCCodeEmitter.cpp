//===-- TMS9900MCCodeEmitter.cpp - Convert TMS9900 code to machine code ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TMS9900MCCodeEmitter class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/TMS9900MCTargetDesc.h"
#include "MCTargetDesc/TMS9900FixupKinds.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "mccodeemitter"

namespace llvm {

class TMS9900MCCodeEmitter : public MCCodeEmitter {
  MCContext &Ctx;
  MCInstrInfo const &MCII;

  // Offset keeps track of current word number being emitted
  // inside a particular instruction.
  mutable unsigned Offset;

  /// TableGen'erated function for getting the binary encoding for an
  /// instruction.
  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

  /// Returns the binary encoding of operands.
  ///
  /// If an operand requires relocation, the relocation is recorded
  /// and zero is returned.
  unsigned getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

  unsigned getMemOpValue(const MCInst &MI, unsigned Op,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const;

  unsigned getPCRelImmOpValue(const MCInst &MI, unsigned Op,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;

  unsigned getCRUDispEncoding(const MCInst &MI, unsigned Op,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;

  unsigned getBranchTargetEncoding(const MCInst &MI, unsigned Op,
                                   SmallVectorImpl<MCFixup> &Fixups,
                                   const MCSubtargetInfo &STI) const;

  unsigned getBranchTarget16Encoding(const MCInst &MI, unsigned Op,
                                     SmallVectorImpl<MCFixup> &Fixups,
                                     const MCSubtargetInfo &STI) const;

  unsigned getCallTargetEncoding(const MCInst &MI, unsigned Op,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

public:
  TMS9900MCCodeEmitter(MCContext &ctx, MCInstrInfo const &MCII)
      : Ctx(ctx), MCII(MCII) {}

  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;
};

void TMS9900MCCodeEmitter::encodeInstruction(const MCInst &MI,
                                              SmallVectorImpl<char> &CB,
                                              SmallVectorImpl<MCFixup> &Fixups,
                                              const MCSubtargetInfo &STI) const {
  const MCInstrDesc &Desc = MCII.get(MI.getOpcode());
  // Get byte count of instruction.
  unsigned Size = Desc.getSize();

  // Initialize fixup offset
  Offset = 2;

  uint64_t BinaryOpCode = getBinaryCodeForInstr(MI, Fixups, STI);
  size_t WordCount = Size / 2;

  // TMS9900 is big-endian
  while (WordCount--) {
    support::endian::write(CB, (uint16_t)BinaryOpCode,
                           llvm::endianness::big);
    BinaryOpCode >>= 16;
  }
}

unsigned TMS9900MCCodeEmitter::getMachineOpValue(const MCInst &MI,
                                                  const MCOperand &MO,
                                                  SmallVectorImpl<MCFixup> &Fixups,
                                                  const MCSubtargetInfo &STI) const {
  if (MO.isReg())
    return Ctx.getRegisterInfo()->getEncodingValue(MO.getReg());

  if (MO.isImm()) {
    Offset += 2;
    return MO.getImm();
  }

  assert(MO.isExpr() && "Expected expr operand");
  Fixups.push_back(MCFixup::create(Offset, MO.getExpr(),
      static_cast<MCFixupKind>(TMS9900::fixup_tms9900_16), MI.getLoc()));
  Offset += 2;
  return 0;
}

unsigned TMS9900MCCodeEmitter::getMemOpValue(const MCInst &MI, unsigned Op,
                                              SmallVectorImpl<MCFixup> &Fixups,
                                              const MCSubtargetInfo &STI) const {
  const MCOperand &MO1 = MI.getOperand(Op);
  assert(MO1.isReg() && "Register operand expected");
  unsigned Reg = Ctx.getRegisterInfo()->getEncodingValue(MO1.getReg());

  const MCOperand &MO2 = MI.getOperand(Op + 1);
  if (MO2.isImm()) {
    Offset += 2;
    return ((unsigned)MO2.getImm() << 4) | Reg;
  }

  assert(MO2.isExpr() && "Expr operand expected");
  Fixups.push_back(MCFixup::create(Offset, MO2.getExpr(),
      static_cast<MCFixupKind>(TMS9900::fixup_tms9900_16), MI.getLoc()));
  Offset += 2;
  return Reg;
}

unsigned TMS9900MCCodeEmitter::getPCRelImmOpValue(const MCInst &MI, unsigned Op,
                                                   SmallVectorImpl<MCFixup> &Fixups,
                                                   const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(Op);
  if (MO.isImm())
    return MO.getImm();

  assert(MO.isExpr() && "Expr operand expected");
  Fixups.push_back(MCFixup::create(0, MO.getExpr(),
      static_cast<MCFixupKind>(TMS9900::fixup_tms9900_pcrel_8), MI.getLoc()));
  return 0;
}

unsigned TMS9900MCCodeEmitter::getCRUDispEncoding(const MCInst &MI, unsigned Op,
                                                   SmallVectorImpl<MCFixup> &Fixups,
                                                   const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(Op);
  if (MO.isImm())
    return MO.getImm() & 0xFF;

  assert(MO.isExpr() && "Expr operand expected");
  Fixups.push_back(MCFixup::create(0, MO.getExpr(),
      static_cast<MCFixupKind>(TMS9900::fixup_tms9900_8), MI.getLoc()));
  return 0;
}

unsigned TMS9900MCCodeEmitter::getBranchTargetEncoding(const MCInst &MI, unsigned Op,
                                                        SmallVectorImpl<MCFixup> &Fixups,
                                                        const MCSubtargetInfo &STI) const {
  // Branch target for TMS9900 jump instructions (Format 6)
  // The displacement is 8-bit signed, in words
  const MCOperand &MO = MI.getOperand(Op);
  if (MO.isImm())
    return MO.getImm() & 0xFF;

  assert(MO.isExpr() && "Expr operand expected");
  Fixups.push_back(MCFixup::create(0, MO.getExpr(),
      static_cast<MCFixupKind>(TMS9900::fixup_tms9900_pcrel_8), MI.getLoc()));
  return 0;
}

unsigned TMS9900MCCodeEmitter::getBranchTarget16Encoding(const MCInst &MI, unsigned Op,
                                                          SmallVectorImpl<MCFixup> &Fixups,
                                                          const MCSubtargetInfo &STI) const {
  // Absolute branch target for B @addr instructions (used in branch relaxation)
  // The address is a 16-bit absolute value in the second word
  const MCOperand &MO = MI.getOperand(Op);
  if (MO.isImm())
    return MO.getImm();

  assert(MO.isExpr() && "Expr operand expected");
  // Fixup at offset 2 (second word of the instruction)
  Fixups.push_back(MCFixup::create(2, MO.getExpr(),
      static_cast<MCFixupKind>(TMS9900::fixup_tms9900_16), MI.getLoc()));
  return 0;
}

unsigned TMS9900MCCodeEmitter::getCallTargetEncoding(const MCInst &MI, unsigned Op,
                                                      SmallVectorImpl<MCFixup> &Fixups,
                                                      const MCSubtargetInfo &STI) const {
  // Call target for BL @symbol instructions
  // The address is an absolute 16-bit value in the second word
  const MCOperand &MO = MI.getOperand(Op);
  if (MO.isImm())
    return MO.getImm();

  assert(MO.isExpr() && "Expr operand expected");
  // Fixup at offset 2 (second word of the instruction)
  Fixups.push_back(MCFixup::create(2, MO.getExpr(),
      static_cast<MCFixupKind>(TMS9900::fixup_tms9900_16), MI.getLoc()));
  return 0;
}

MCCodeEmitter *createTMS9900MCCodeEmitter(const MCInstrInfo &MCII,
                                          MCContext &Ctx) {
  return new TMS9900MCCodeEmitter(Ctx, MCII);
}

#include "TMS9900GenMCCodeEmitter.inc"

} // end of namespace llvm
