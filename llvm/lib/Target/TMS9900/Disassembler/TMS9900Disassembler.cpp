//===-- TMS9900Disassembler.cpp - Disassembler for TMS9900 ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TMS9900Disassembler class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/TMS9900MCTargetDesc.h"
#include "TargetInfo/TMS9900TargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Endian.h"

using namespace llvm;

#define DEBUG_TYPE "tms9900-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {
class TMS9900Disassembler : public MCDisassembler {
public:
  TMS9900Disassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}

  DecodeStatus getInstruction(MCInst &MI, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};
} // end anonymous namespace

static MCDisassembler *createTMS9900Disassembler(const Target &T,
                                                  const MCSubtargetInfo &STI,
                                                  MCContext &Ctx) {
  return new TMS9900Disassembler(STI, Ctx);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeTMS9900Disassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheTMS9900Target(),
                                         createTMS9900Disassembler);
}

// Register decoder table
static const unsigned GR16DecoderTable[] = {
    TMS9900::R0,  TMS9900::R1,  TMS9900::R2,  TMS9900::R3,
    TMS9900::R4,  TMS9900::R5,  TMS9900::R6,  TMS9900::R7,
    TMS9900::R8,  TMS9900::R9,  TMS9900::R10, TMS9900::R11,
    TMS9900::R12, TMS9900::R13, TMS9900::R14, TMS9900::R15
};

static DecodeStatus DecodeGR16RegisterClass(MCInst &MI, uint64_t RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  if (RegNo > 15)
    return MCDisassembler::Fail;
  MI.addOperand(MCOperand::createReg(GR16DecoderTable[RegNo]));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeIdxRegsRegisterClass(MCInst &MI, uint64_t RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  if (RegNo == 0 || RegNo > 15)
    return MCDisassembler::Fail;
  MI.addOperand(MCOperand::createReg(GR16DecoderTable[RegNo]));
  return MCDisassembler::Success;
}

static DecodeStatus decodeBranchTarget(MCInst &MI, uint64_t Bits,
                                       uint64_t Address,
                                       const MCDisassembler *Decoder) {
  int8_t Disp = static_cast<int8_t>(Bits & 0xFF);
  int64_t Target = static_cast<int64_t>(Address) + 2 + (Disp * 2);
  MI.addOperand(MCOperand::createImm(Target));
  return MCDisassembler::Success;
}

static DecodeStatus decodeCRUDisp(MCInst &MI, uint64_t Bits, uint64_t Address,
                                  const MCDisassembler *Decoder) {
  int8_t Disp = static_cast<int8_t>(Bits & 0xFF);
  MI.addOperand(MCOperand::createImm(Disp));
  return MCDisassembler::Success;
}

static DecodeStatus decodeShiftCount(MCInst &MI, uint64_t Bits,
                                     uint64_t Address,
                                     const MCDisassembler *Decoder) {
  uint64_t Count = Bits & 0xF;
  MI.addOperand(MCOperand::createImm(Count));
  return MCDisassembler::Success;
}

#include "TMS9900GenDisassemblerTables.inc"

static const uint16_t RTWP_OPCODE = 0x0380;
static const uint16_t IDLE_OPCODE = 0x0340;
static const uint16_t RSET_OPCODE = 0x0360;
static const uint16_t CKOF_OPCODE = 0x03C0;
static const uint16_t CKON_OPCODE = 0x03A0;
static const uint16_t LREX_OPCODE = 0x03E0;
static const uint16_t STWP_OPCODE = 0x02A0;
static const uint16_t STST_OPCODE = 0x02C0;
static const uint16_t LWPI_OPCODE = 0x02E0;
static const uint16_t LIMI_OPCODE = 0x0300;

static unsigned getInstructionWords(uint16_t Insn) {
  switch (Insn) {
  case RTWP_OPCODE:
  case IDLE_OPCODE:
  case RSET_OPCODE:
  case CKOF_OPCODE:
  case CKON_OPCODE:
  case LREX_OPCODE:
    return 1;
  default:
    break;
  }

  if ((Insn & 0xFFF0) == STWP_OPCODE || (Insn & 0xFFF0) == STST_OPCODE)
    return 1;

  if ((Insn & 0xFFE0) == LWPI_OPCODE || (Insn & 0xFFE0) == LIMI_OPCODE)
    return 2;

  if ((Insn & 0xFF00) == 0x0200) {
    unsigned Subop = (Insn >> 4) & 0xF;
    if (Subop == 0x0 || Subop == 0x2 || Subop == 0x4 || Subop == 0x6 ||
        Subop == 0x8)
      return 2;
  }

  if ((Insn & 0xFF00) == 0x1D00 || (Insn & 0xFF00) == 0x1E00 ||
      (Insn & 0xFF00) == 0x1F00)
    return 1;

  if ((Insn & 0xF000) == 0x1000)
    return 1;

  if ((Insn & 0xFC00) == 0x0800)
    return 1;

  unsigned Op4 = (Insn >> 12) & 0xF;
  if (Op4 >= 0x4) {
    unsigned Ts = (Insn >> 4) & 0x3;
    unsigned Td = (Insn >> 10) & 0x3;
    return 1 + (Ts == 2) + (Td == 2);
  }

  unsigned Ts = (Insn >> 4) & 0x3;
  return 1 + (Ts == 2);
}

DecodeStatus TMS9900Disassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                                  ArrayRef<uint8_t> Bytes,
                                                  uint64_t Address,
                                                  raw_ostream &CStream) const {
  if (Bytes.size() < 2) {
    Size = 0;
    return MCDisassembler::Fail;
  }

  uint16_t Insn16 = support::endian::read16be(Bytes.data());
  unsigned Words = getInstructionWords(Insn16);
  unsigned RequiredBytes = Words * 2;

  if (Bytes.size() < RequiredBytes) {
    Size = 2;
    return MCDisassembler::Fail;
  }

  uint64_t Insn = Insn16;
  if (Words >= 2)
    Insn |= (uint64_t)support::endian::read16be(Bytes.data() + 2) << 16;
  if (Words >= 3)
    Insn |= (uint64_t)support::endian::read16be(Bytes.data() + 4) << 32;

  const uint8_t *DecoderTable = nullptr;
  switch (Words) {
  case 1:
    DecoderTable = DecoderTable16;
    break;
  case 2:
    DecoderTable = DecoderTable32;
    break;
  case 3:
    DecoderTable = DecoderTable48;
    break;
  default:
    Size = RequiredBytes;
    return MCDisassembler::Fail;
  }

  DecodeStatus Result =
      decodeInstruction(DecoderTable, MI, Insn, Address, this, STI);
  Size = RequiredBytes;
  return Result;
}
