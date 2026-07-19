//===- I8085Disassembler.cpp - Disassembler for I8085 -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is part of the I8085 Disassembler.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/I8085MCTargetDesc.h"
#include "TargetInfo/I8085TargetInfo.h"

#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "i8085-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {

class I8085Disassembler : public MCDisassembler {
public:
  I8085Disassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}

  DecodeStatus getInstruction(MCInst &MI, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};

} // end anonymous namespace

static MCDisassembler *createI8085Disassembler(const Target &T,
                                                const MCSubtargetInfo &STI,
                                                MCContext &Ctx) {
  return new I8085Disassembler(STI, Ctx);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeI8085Disassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheI8085Target(),
                                         createI8085Disassembler);
}

static const unsigned GR8DecoderTable[] = {
    I8085::B, I8085::C, I8085::D, I8085::E,
    I8085::H, I8085::L, I8085::M, I8085::A,
};

static DecodeStatus DecodeGR8RegisterClass(MCInst &MI, uint64_t RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  if (RegNo > 7)
    return MCDisassembler::Fail;

  MI.addOperand(MCOperand::createReg(GR8DecoderTable[RegNo]));
  return MCDisassembler::Success;
}

static const unsigned GR16DecoderTable[] = {
    I8085::BC, I8085::DE, I8085::HL, I8085::SP,
};

static DecodeStatus DecodeGR16RegisterClass(MCInst &MI, uint64_t RegNo,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  if (RegNo > 3)
    return MCDisassembler::Fail;

  MI.addOperand(MCOperand::createReg(GR16DecoderTable[RegNo]));
  return MCDisassembler::Success;
}

static const unsigned GR16BDDecoderTable[] = {
    I8085::BC, I8085::DE,
};

static DecodeStatus DecodeGR16BDRegisterClass(MCInst &MI, uint64_t RegNo,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder) {
  if (RegNo > 1)
    return MCDisassembler::Fail;

  MI.addOperand(MCOperand::createReg(GR16BDDecoderTable[RegNo]));
  return MCDisassembler::Success;
}

static const unsigned GR16PPDecoderTable[] = {
    I8085::BC, I8085::DE, I8085::HL, I8085::PSW,
};

static DecodeStatus DecodeGR16PPRegisterClass(MCInst &MI, uint64_t RegNo,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder) {
  if (RegNo > 3)
    return MCDisassembler::Fail;

  MI.addOperand(MCOperand::createReg(GR16PPDecoderTable[RegNo]));
  return MCDisassembler::Success;
}

#include "I8085GenDisassemblerTables.inc"

static unsigned getInstructionSize(uint8_t Opcode) {
  switch (Opcode) {
  case 0x01:
  case 0x11:
  case 0x21:
  case 0x31:
    return 3; // LXI
  case 0x22:
  case 0x2A:
  case 0x32:
  case 0x3A:
    return 3; // SHLD/LHLD/STA/LDA
  case 0xC3:
  case 0xC2:
  case 0xCA:
  case 0xD2:
  case 0xDA:
  case 0xE2:
  case 0xEA:
  case 0xF2:
  case 0xFA:
    return 3; // JMP/Jcc
  case 0xCD:
  case 0xC4:
  case 0xCC:
  case 0xD4:
  case 0xDC:
  case 0xE4:
  case 0xEC:
  case 0xF4:
  case 0xFC:
    return 3; // CALL/Ccc
  default:
    break;
  }

  if ((Opcode & 0xC7) == 0x06)
    return 2; // MVI r, imm8

  switch (Opcode) {
  case 0xC6:
  case 0xCE:
  case 0xD6:
  case 0xDE:
  case 0xE6:
  case 0xEE:
  case 0xF6:
  case 0xFE:
    return 2; // ALU imm8
  case 0xDB:
  case 0xD3:
    return 2; // IN/OUT
  case 0x28:
  case 0x38:
    return 2; // LDHI/LDSI (undoc, 2-byte immediate)
  case 0xDD:
  case 0xFD:
    return 3; // JNX5/JX5 (undoc, 3-byte conditional jump)
  default:
    break;
  }

  return 1;
}

DecodeStatus I8085Disassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                               ArrayRef<uint8_t> Bytes,
                                               uint64_t Address,
                                               raw_ostream &CStream) const {
  Size = 0;
  if (Bytes.empty())
    return MCDisassembler::Fail;

  uint8_t Opcode = Bytes[0];

  // MOV M, M is not a valid instruction; 0x76 is HLT.
  if (Opcode == 0x76) {
    MI.setOpcode(I8085::HLT);
    Size = 1;
    return MCDisassembler::Success;
  }

  unsigned InstSize = getInstructionSize(Opcode);
  if (Bytes.size() < InstSize)
    return MCDisassembler::Fail;

  uint64_t Insn = 0;
  DecodeStatus S = MCDisassembler::Fail;

  switch (InstSize) {
  case 1:
    Insn = Opcode;
    S = decodeInstruction(DecoderTable8, MI, Insn, Address, this,
                          STI);
    break;
  case 2:
    Insn = (static_cast<uint16_t>(Opcode) << 8) | Bytes[1];
    S = decodeInstruction(DecoderTable16, MI, Insn, Address, this,
                          STI);
    break;
  case 3:
    Insn = (static_cast<uint32_t>(Opcode) << 16) |
           (static_cast<uint32_t>(Bytes[2]) << 8) |
           static_cast<uint32_t>(Bytes[1]);
    S = decodeInstruction(DecoderTable24, MI, Insn, Address, this,
                          STI);
    break;
  default:
    return MCDisassembler::Fail;
  }

  if (S == MCDisassembler::Fail)
    return MCDisassembler::Fail;

  Size = InstSize;
  return S;
}
