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

// TMS9900 Addressing Modes (Ts/Td field):
// 00 = Register direct:     Rn
// 01 = Register indirect:   *Rn
// 10 = Symbolic/Indexed:    @addr or @addr(Rn)
// 11 = Register indirect with auto-increment: *Rn+

// Opcode tables for each format
// Format 1: [opcode:4][B:1][Td:2][D:4][Ts:2][S:4]
struct Format1Info {
  uint16_t opcode;
  unsigned mcOpcode;
  const char *name;
};

static const Format1Info Format1Opcodes[] = {
    {0xA, TMS9900::Arr,    "A"},      // 1010
    {0x8, TMS9900::Crr,    "C"},      // 1000
    {0x6, TMS9900::Srr,    "S"},      // 0110
    {0xE, TMS9900::SOCrr,  "SOC"},    // 1110
    {0x4, TMS9900::SZCrr,  "SZC"},    // 0100
    {0xC, TMS9900::MOVrr,  "MOV"},    // 1100
    // Note: Byte instructions (AB, CB, SB, SOCB, SZCB, MOVB) not yet implemented
};

// Format 3: [opcode:10][Ts:2][S:4] - Single operand
struct Format3Info {
  uint16_t opcode;
  unsigned mcOpcode;
  const char *name;
};

static const Format3Info Format3Opcodes[] = {
    {0x0440, TMS9900::Br,    "B"},     // 0000010001
    {0x0680, TMS9900::BL,    "BL"},    // 0000011010
    // Note: BLWP and X instructions not implemented in current TableGen
    {0x04C0, TMS9900::CLRr,  "CLR"},   // 0000010011
    {0x0700, TMS9900::SETOr, "SETO"},  // 0000011100
    {0x0540, TMS9900::INVr,  "INV"},   // 0000010101
    {0x0500, TMS9900::NEGr,  "NEG"},   // 0000010100
    {0x0580, TMS9900::ABSr,  "ABS"},   // 0000010110
    {0x05C0, TMS9900::SWPBr, "SWPB"},  // 0000010111
    {0x0600, TMS9900::INCr,  "INC"},   // 0000011000
    {0x0640, TMS9900::INCTr, "INCT"},  // 0000011001
    {0x0740, TMS9900::DECr,  "DEC"},   // 0000011101
    {0x0780, TMS9900::DECTr, "DECT"},  // 0000011110
};

// Format 6: [opcode:8][disp:8] - Jump instructions
struct Format6Info {
  uint16_t opcode;
  unsigned mcOpcode;
  const char *name;
};

static const Format6Info Format6Opcodes[] = {
    {0x10, TMS9900::JMP,  "JMP"},   // 00010000
    {0x11, TMS9900::JLT,  "JLT"},   // 00010001
    {0x12, TMS9900::JLE,  "JLE"},   // 00010010
    {0x13, TMS9900::JEQ,  "JEQ"},   // 00010011
    {0x14, TMS9900::JHE,  "JHE"},   // 00010100
    {0x15, TMS9900::JGT,  "JGT"},   // 00010101
    {0x16, TMS9900::JNE,  "JNE"},   // 00010110
    {0x17, TMS9900::JNC,  "JNC"},   // 00010111
    {0x18, TMS9900::JOC,  "JOC"},   // 00011000
    {0x19, TMS9900::JNO,  "JNO"},   // 00011001
    {0x1A, TMS9900::JL,   "JL"},    // 00011010
    {0x1B, TMS9900::JH,   "JH"},    // 00011011
    {0x1C, TMS9900::JOP,  "JOP"},   // 00011100
};

// Format 8: [0000 0010][D:4][subop:4] + [imm:16] - Immediate to register
struct Format8Info {
  uint16_t subop;
  unsigned mcOpcode;
  const char *name;
};

static const Format8Info Format8Opcodes[] = {
    {0x0, TMS9900::LI,   "LI"},    // 0000
    {0x1, TMS9900::AI,   "AI"},    // 0001 - actually 0010 per encoding
    {0x2, TMS9900::AI,   "AI"},    // 0010
    {0x4, TMS9900::ANDI, "ANDI"},  // 0100
    {0x6, TMS9900::ORI,  "ORI"},   // 0110
    {0x8, TMS9900::CI,   "CI"},    // 1000
};

// Format 9: [opcode:12] + [imm:16] - LWPI, LIMI
static const uint16_t LWPI_OPCODE = 0x02E0;
static const uint16_t LIMI_OPCODE = 0x0300;

// Format 7: [opcode:8][C:4][W:4] - Shift instructions
struct Format7Info {
  uint16_t opcode;
  unsigned mcOpcode;
  const char *name;
};

static const Format7Info Format7Opcodes[] = {
    {0x0A, TMS9900::SLAri, "SLA"},   // 00001010
    {0x08, TMS9900::SRAri, "SRA"},   // 00001000
    {0x0B, TMS9900::SRCri, "SRC"},   // 00001011
    {0x09, TMS9900::SRLri, "SRL"},   // 00001001
};

// Other single-word instructions
static const uint16_t RTWP_OPCODE = 0x0380;
static const uint16_t IDLE_OPCODE = 0x0340;
static const uint16_t RSET_OPCODE = 0x0360;
static const uint16_t CKOF_OPCODE = 0x03C0;
static const uint16_t CKON_OPCODE = 0x03A0;
static const uint16_t LREX_OPCODE = 0x03E0;

DecodeStatus TMS9900Disassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                                  ArrayRef<uint8_t> Bytes,
                                                  uint64_t Address,
                                                  raw_ostream &CStream) const {
  // Need at least 2 bytes
  if (Bytes.size() < 2) {
    Size = 0;
    return MCDisassembler::Fail;
  }

  // TMS9900 is big-endian
  uint16_t Insn = support::endian::read16be(Bytes.data());
  Size = 2;  // Minimum instruction size

  // Check Format 9: LWPI, LIMI (need second word)
  if ((Insn & 0xFFE0) == 0x02E0) {  // LWPI
    if (Bytes.size() < 4) return MCDisassembler::Fail;
    uint16_t Imm = support::endian::read16be(Bytes.data() + 2);
    Size = 4;
    MI.setOpcode(TMS9900::LWPI);
    MI.addOperand(MCOperand::createImm(Imm));
    return MCDisassembler::Success;
  }
  if ((Insn & 0xFFE0) == 0x0300) {  // LIMI
    if (Bytes.size() < 4) return MCDisassembler::Fail;
    uint16_t Imm = support::endian::read16be(Bytes.data() + 2);
    Size = 4;
    MI.setOpcode(TMS9900::LIMI);
    MI.addOperand(MCOperand::createImm(Imm));
    return MCDisassembler::Success;
  }

  // Check Format 8: Immediate instructions (0000 0010 DDDD ssss)
  if ((Insn & 0xFF00) == 0x0200) {
    if (Bytes.size() < 4) return MCDisassembler::Fail;
    uint16_t Imm = support::endian::read16be(Bytes.data() + 2);
    Size = 4;
    unsigned Rd = (Insn >> 4) & 0xF;
    unsigned Subop = Insn & 0xF;

    unsigned Opcode = 0;
    switch (Subop) {
    case 0x0: Opcode = TMS9900::LI; break;
    case 0x2: Opcode = TMS9900::AI; break;
    case 0x4: Opcode = TMS9900::ANDI; break;
    case 0x6: Opcode = TMS9900::ORI; break;
    case 0x8: Opcode = TMS9900::CI; break;
    default:
      return MCDisassembler::Fail;
    }
    MI.setOpcode(Opcode);
    if (DecodeGR16RegisterClass(MI, Rd, Address, this) != MCDisassembler::Success)
      return MCDisassembler::Fail;
    MI.addOperand(MCOperand::createImm(Imm));
    return MCDisassembler::Success;
  }

  // Check Format 6: Jump instructions (0001 xxxx dddd dddd)
  if ((Insn & 0xF000) == 0x1000) {
    unsigned JmpOpcode = (Insn >> 8) & 0xFF;
    int8_t Disp = Insn & 0xFF;  // Signed 8-bit displacement

    unsigned Opcode = 0;
    for (const auto &Info : Format6Opcodes) {
      if (Info.opcode == JmpOpcode) {
        Opcode = Info.mcOpcode;
        break;
      }
    }
    if (Opcode == 0)
      return MCDisassembler::Fail;

    MI.setOpcode(Opcode);
    // Calculate target address: PC + 2 + (disp * 2)
    int64_t Target = Address + 2 + (Disp * 2);
    MI.addOperand(MCOperand::createImm(Target));
    return MCDisassembler::Success;
  }

  // Check Format 7: Shift instructions (0000 10xx cccc wwww)
  if ((Insn & 0xFC00) == 0x0800) {
    unsigned ShiftOp = (Insn >> 8) & 0x0F;
    unsigned Count = (Insn >> 4) & 0xF;
    unsigned Reg = Insn & 0xF;

    unsigned Opcode = 0;
    switch (ShiftOp) {
    case 0x08: Opcode = TMS9900::SRAri; break;
    case 0x09: Opcode = TMS9900::SRLri; break;
    case 0x0A: Opcode = TMS9900::SLAri; break;
    case 0x0B: Opcode = TMS9900::SRCri; break;
    default:
      return MCDisassembler::Fail;
    }
    MI.setOpcode(Opcode);
    if (DecodeGR16RegisterClass(MI, Reg, Address, this) != MCDisassembler::Success)
      return MCDisassembler::Fail;
    MI.addOperand(MCOperand::createImm(Count == 0 ? 16 : Count));
    return MCDisassembler::Success;
  }

  // Check Format 11: Single-word instructions
  if (Insn == RTWP_OPCODE) {
    MI.setOpcode(TMS9900::RTWP);
    return MCDisassembler::Success;
  }

  // Check Format 3: Single operand (0000 01xx xxxx xxxx)
  if ((Insn & 0xFC00) == 0x0400 || (Insn & 0xFC00) == 0x0500 ||
      (Insn & 0xFC00) == 0x0600 || (Insn & 0xFC00) == 0x0700) {
    unsigned Op10 = (Insn >> 6) & 0x3FF;  // 10-bit opcode
    unsigned Ts = (Insn >> 4) & 0x3;
    unsigned S = Insn & 0xF;

    unsigned Opcode = 0;
    for (const auto &Info : Format3Opcodes) {
      if ((Info.opcode >> 6) == (Op10 >> 0) &&
          (Info.opcode & 0x3F) == ((Ts << 4) | 0)) {
        // Match opcode ignoring Ts/S fields
      }
    }

    // Match by full opcode pattern
    uint16_t Op6 = Insn & 0xFFC0;  // Opcode without Ts/S
    switch (Op6) {
    // Note: BLWP (0x0400) and X (0x06C0) not implemented in current TableGen
    case 0x0440: Opcode = TMS9900::Br; break;
    case 0x04C0: Opcode = TMS9900::CLRr; break;
    case 0x0500: Opcode = TMS9900::NEGr; break;
    case 0x0540: Opcode = TMS9900::INVr; break;
    case 0x0580: Opcode = TMS9900::ABSr; break;
    case 0x05C0: Opcode = TMS9900::SWPBr; break;
    case 0x0600: Opcode = TMS9900::INCr; break;
    case 0x0640: Opcode = TMS9900::INCTr; break;
    case 0x0680: Opcode = TMS9900::BL; break;
    case 0x0700: Opcode = TMS9900::SETOr; break;
    case 0x0740: Opcode = TMS9900::DECr; break;
    case 0x0780: Opcode = TMS9900::DECTr; break;
    default:
      break;
    }

    if (Opcode == 0)
      return MCDisassembler::Fail;

    // Special case: BL always uses symbolic addressing with target in second word
    // (our BL_sym encoding puts Ts=0 but target is in word 2)
    if (Opcode == TMS9900::BL) {
      if (Bytes.size() < 4) return MCDisassembler::Fail;
      uint16_t Target = support::endian::read16be(Bytes.data() + 2);
      Size = 4;
      MI.setOpcode(TMS9900::BL_sym);
      MI.addOperand(MCOperand::createImm(Target));
      return MCDisassembler::Success;
    }

    // Special case: B (branch) also often uses symbolic addressing
    if (Opcode == TMS9900::Br && Ts == 0 && S == 0) {
      // B @addr - symbolic addressing
      if (Bytes.size() < 4) return MCDisassembler::Fail;
      uint16_t Target = support::endian::read16be(Bytes.data() + 2);
      Size = 4;
      MI.setOpcode(TMS9900::Br);
      MI.addOperand(MCOperand::createImm(Target));
      return MCDisassembler::Success;
    }

    MI.setOpcode(Opcode);

    // Handle addressing mode
    if (Ts == 0) {
      // Register direct
      if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
        return MCDisassembler::Fail;
    } else if (Ts == 2 && S == 0) {
      // Symbolic addressing - need second word
      if (Bytes.size() < 4) return MCDisassembler::Fail;
      uint16_t Addr = support::endian::read16be(Bytes.data() + 2);
      Size = 4;
      MI.addOperand(MCOperand::createImm(Addr));
    } else {
      // Other addressing modes - simplified for now
      // TODO: Handle indirect (*Rn), indexed (@addr(Rn)), post-increment (*Rn+)
      if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
        return MCDisassembler::Fail;
    }
    return MCDisassembler::Success;
  }

  // Check Format 1: Dual operand (high nibble 4-F)
  unsigned Op4 = (Insn >> 12) & 0xF;
  if (Op4 >= 4) {
    unsigned ByteOp = (Insn >> 12) & 0x1;  // B bit for byte operations
    unsigned Td = (Insn >> 10) & 0x3;
    unsigned D = (Insn >> 6) & 0xF;
    unsigned Ts = (Insn >> 4) & 0x3;
    unsigned S = Insn & 0xF;

    unsigned Opcode = 0;
    for (const auto &Info : Format1Opcodes) {
      if (Info.opcode == Op4) {
        Opcode = Info.mcOpcode;
        break;
      }
    }

    // Handle MOVB (opcode 0xD) - byte move operations
    // Select appropriate MOVB variant based on addressing modes
    if (Op4 == 0xD) {
      // MOVB uses different MCInst variants for different addressing modes
      // For now, try to match the addressing mode pattern
      if (Ts == 0 && Td == 1) {
        // MOVB Rn, *Rm - register to indirect
        Opcode = TMS9900::MOVBmi;  // Best match we have
      } else if (Ts == 0 && Td == 0) {
        // MOVB Rn, Rm - register to register (use MOVrr as fallback)
        Opcode = TMS9900::MOVrr;  // No MOVBrr, use word version
      } else {
        // Other addressing modes - return fail for now
        return MCDisassembler::Fail;
      }
    }

    if (Opcode == 0)
      return MCDisassembler::Fail;

    MI.setOpcode(Opcode);

    // Calculate instruction size based on addressing modes
    unsigned ExtraWords = 0;
    if (Ts == 2) ExtraWords++;  // Symbolic/indexed source
    if (Td == 2) ExtraWords++;  // Symbolic/indexed dest

    if (ExtraWords > 0 && Bytes.size() < 2 + ExtraWords * 2)
      return MCDisassembler::Fail;

    Size = 2 + ExtraWords * 2;

    // Add source operand
    if (Ts == 0) {
      // Register direct
      if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
        return MCDisassembler::Fail;
    } else if (Ts == 2) {
      // Symbolic or indexed
      uint16_t Addr = support::endian::read16be(Bytes.data() + 2);
      if (S == 0) {
        // Symbolic
        MI.addOperand(MCOperand::createImm(Addr));
      } else {
        // Indexed
        MI.addOperand(MCOperand::createImm(Addr));
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
      }
    } else {
      // Indirect or auto-increment
      if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
        return MCDisassembler::Fail;
    }

    // Add dest operand
    if (Td == 0) {
      // Register direct
      if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
        return MCDisassembler::Fail;
    } else if (Td == 2) {
      // Symbolic or indexed
      unsigned Offset = (Ts == 2) ? 4 : 2;
      uint16_t Addr = support::endian::read16be(Bytes.data() + Offset);
      if (D == 0) {
        MI.addOperand(MCOperand::createImm(Addr));
      } else {
        MI.addOperand(MCOperand::createImm(Addr));
        if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
      }
    } else {
      if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
        return MCDisassembler::Fail;
    }

    return MCDisassembler::Success;
  }

  // Unknown instruction
  return MCDisassembler::Fail;
}
