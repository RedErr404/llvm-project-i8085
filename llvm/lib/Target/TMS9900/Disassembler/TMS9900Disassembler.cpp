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
static const uint16_t STST_OPCODE = 0x02C0;
static const uint16_t STWP_OPCODE = 0x02A0;

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

  // Check Format 10: STST/STWP (store internal registers)
  if ((Insn & 0xFFF0) == STST_OPCODE) {
    unsigned Rd = Insn & 0xF;
    MI.setOpcode(TMS9900::STST);
    if (DecodeGR16RegisterClass(MI, Rd, Address, this) != MCDisassembler::Success)
      return MCDisassembler::Fail;
    return MCDisassembler::Success;
  }
  if ((Insn & 0xFFF0) == STWP_OPCODE) {
    unsigned Rd = Insn & 0xF;
    MI.setOpcode(TMS9900::STWP);
    if (DecodeGR16RegisterClass(MI, Rd, Address, this) != MCDisassembler::Success)
      return MCDisassembler::Fail;
    return MCDisassembler::Success;
  }

  // Check Format 8: Immediate instructions (0000 0010 ssss rrrr)
  // where ssss = sub-opcode in bits 7-4, rrrr = register in bits 3-0
  if ((Insn & 0xFF00) == 0x0200) {
    if (Bytes.size() < 4) return MCDisassembler::Fail;
    uint16_t Imm = support::endian::read16be(Bytes.data() + 2);
    Size = 4;
    unsigned Subop = (Insn >> 4) & 0xF;
    unsigned Rd = Insn & 0xF;

    unsigned Opcode = 0;
    bool needsTiedOperand = false;  // For AI/ANDI/ORI which have $rd = $rs
    switch (Subop) {
    case 0x0: Opcode = TMS9900::LI; break;  // (outs $rd), (ins $imm)
    case 0x2: Opcode = TMS9900::AI; needsTiedOperand = true; break;  // (outs $rd), (ins $rs, $imm)
    case 0x4: Opcode = TMS9900::ANDI; needsTiedOperand = true; break;
    case 0x6: Opcode = TMS9900::ORI; needsTiedOperand = true; break;
    case 0x8: Opcode = TMS9900::CI; break;  // (outs), (ins $rs, $imm)
    default:
      return MCDisassembler::Fail;
    }
    MI.setOpcode(Opcode);
    // For AI/ANDI/ORI: add register twice (once for $rd output, once for $rs input)
    if (needsTiedOperand) {
      if (DecodeGR16RegisterClass(MI, Rd, Address, this) != MCDisassembler::Success)
        return MCDisassembler::Fail;
    }
    if (DecodeGR16RegisterClass(MI, Rd, Address, this) != MCDisassembler::Success)
      return MCDisassembler::Fail;
    MI.addOperand(MCOperand::createImm(Imm));
    return MCDisassembler::Success;
  }

  // Check Format 5: CRU single-bit (SBO/SBZ/TB)
  if ((Insn & 0xFF00) == 0x1D00 || (Insn & 0xFF00) == 0x1E00 ||
      (Insn & 0xFF00) == 0x1F00) {
    unsigned Opcode = 0;
    switch ((Insn >> 8) & 0xFF) {
    case 0x1D: Opcode = TMS9900::SBO; break;
    case 0x1E: Opcode = TMS9900::SBZ; break;
    case 0x1F: Opcode = TMS9900::TB; break;
    default:
      return MCDisassembler::Fail;
    }
    MI.setOpcode(Opcode);
    int8_t Disp = static_cast<int8_t>(Insn & 0xFF);
    MI.addOperand(MCOperand::createImm(Disp));
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
    // Shift instructions have (outs $rd), (ins $rs, $cnt) with $rd = $rs
    // So we need to add register twice (for tied operands) then count
    if (DecodeGR16RegisterClass(MI, Reg, Address, this) != MCDisassembler::Success)
      return MCDisassembler::Fail;
    if (DecodeGR16RegisterClass(MI, Reg, Address, this) != MCDisassembler::Success)
      return MCDisassembler::Fail;
    MI.addOperand(MCOperand::createImm(Count == 0 ? 16 : Count));
    return MCDisassembler::Success;
  }

  // Check Format 2/4: Dual operand and CRU multi-bit
  if ((Insn & 0xC000) == 0x0000) {
    unsigned Op6 = (Insn >> 10) & 0x3F;
    unsigned D = (Insn >> 6) & 0xF;
    unsigned Ts = (Insn >> 4) & 0x3;
    unsigned S = Insn & 0xF;

    switch (Op6) {
    case 0x08: { // COC
      if (Ts == 0) {
        MI.setOpcode(TMS9900::COCrr);
        if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      if (Ts == 1) {
        MI.setOpcode(TMS9900::COCim);
        if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      if (Ts == 3) {
        MI.setOpcode(TMS9900::COCpim);
        if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      if (Ts == 2) {
        if (Bytes.size() < 4) return MCDisassembler::Fail;
        uint16_t Extra = support::endian::read16be(Bytes.data() + 2);
        Size = 4;
        if (S == 0) {
          MI.setOpcode(TMS9900::COCam);
          if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
            return MCDisassembler::Fail;
          MI.addOperand(MCOperand::createImm(Extra));
        } else {
          MI.setOpcode(TMS9900::COCxm);
          if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
            return MCDisassembler::Fail;
          MI.addOperand(MCOperand::createImm(Extra));
          if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
            return MCDisassembler::Fail;
        }
        return MCDisassembler::Success;
      }
      return MCDisassembler::Fail;
    }
    case 0x09: { // CZC
      if (Ts == 0) {
        MI.setOpcode(TMS9900::CZCrr);
        if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      if (Ts == 1) {
        MI.setOpcode(TMS9900::CZCim);
        if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      if (Ts == 3) {
        MI.setOpcode(TMS9900::CZCpim);
        if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      if (Ts == 2) {
        if (Bytes.size() < 4) return MCDisassembler::Fail;
        uint16_t Extra = support::endian::read16be(Bytes.data() + 2);
        Size = 4;
        if (S == 0) {
          MI.setOpcode(TMS9900::CZCam);
          if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
            return MCDisassembler::Fail;
          MI.addOperand(MCOperand::createImm(Extra));
        } else {
          MI.setOpcode(TMS9900::CZCxm);
          if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
            return MCDisassembler::Fail;
          MI.addOperand(MCOperand::createImm(Extra));
          if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
            return MCDisassembler::Fail;
        }
        return MCDisassembler::Success;
      }
      return MCDisassembler::Fail;
    }
    case 0x0A: { // XOR
      if (Ts == 0) {
        MI.setOpcode(TMS9900::XORrr);
        if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      if (Ts == 1) {
        MI.setOpcode(TMS9900::XORim);
        if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      if (Ts == 3) {
        MI.setOpcode(TMS9900::XORpim);
        if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      if (Ts == 2) {
        if (Bytes.size() < 4) return MCDisassembler::Fail;
        uint16_t Extra = support::endian::read16be(Bytes.data() + 2);
        Size = 4;
        if (S == 0) {
          MI.setOpcode(TMS9900::XORam);
          if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
            return MCDisassembler::Fail;
          MI.addOperand(MCOperand::createImm(Extra));
        } else {
          MI.setOpcode(TMS9900::XORxm);
          if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
            return MCDisassembler::Fail;
          MI.addOperand(MCOperand::createImm(Extra));
          if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
            return MCDisassembler::Fail;
        }
        return MCDisassembler::Success;
      }
      return MCDisassembler::Fail;
    }
    case 0x0B: { // XOP
      unsigned Opcode = 0;
      if (Ts == 0)
        Opcode = TMS9900::XOPr;
      else if (Ts == 1)
        Opcode = TMS9900::XOPi;
      else if (Ts == 3)
        Opcode = TMS9900::XOPpim;
      else if (Ts == 2) {
        if (Bytes.size() < 4) return MCDisassembler::Fail;
        uint16_t Extra = support::endian::read16be(Bytes.data() + 2);
        Size = 4;
        if (S == 0) {
          MI.setOpcode(TMS9900::XOPam);
          MI.addOperand(MCOperand::createImm(Extra));
        } else {
          MI.setOpcode(TMS9900::XOPxm);
          MI.addOperand(MCOperand::createImm(Extra));
          if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
            return MCDisassembler::Fail;
        }
        MI.addOperand(MCOperand::createImm(D));
        return MCDisassembler::Success;
      }
      if (Opcode == 0)
        return MCDisassembler::Fail;
      MI.setOpcode(Opcode);
      if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
        return MCDisassembler::Fail;
      MI.addOperand(MCOperand::createImm(D));
      return MCDisassembler::Success;
    }
    case 0x0E: { // MPY (hardcoded Rd=R0)
      if (D != 0)
        return MCDisassembler::Fail;
      if (Ts == 0) {
        MI.setOpcode(TMS9900::MPYrr);
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      if (Ts == 1) {
        MI.setOpcode(TMS9900::MPYim);
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      if (Ts == 3) {
        MI.setOpcode(TMS9900::MPYpim);
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      if (Ts == 2) {
        if (Bytes.size() < 4) return MCDisassembler::Fail;
        uint16_t Extra = support::endian::read16be(Bytes.data() + 2);
        Size = 4;
        if (S == 0) {
          MI.setOpcode(TMS9900::MPYam);
          MI.addOperand(MCOperand::createImm(Extra));
        } else {
          MI.setOpcode(TMS9900::MPYxm);
          MI.addOperand(MCOperand::createImm(Extra));
          if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
            return MCDisassembler::Fail;
        }
        return MCDisassembler::Success;
      }
      return MCDisassembler::Fail;
    }
    case 0x0F: { // DIV (hardcoded Rd=R0)
      if (D != 0)
        return MCDisassembler::Fail;
      if (Ts == 0) {
        MI.setOpcode(TMS9900::DIVrr);
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      if (Ts == 1) {
        MI.setOpcode(TMS9900::DIVim);
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      if (Ts == 3) {
        MI.setOpcode(TMS9900::DIVpim);
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      if (Ts == 2) {
        if (Bytes.size() < 4) return MCDisassembler::Fail;
        uint16_t Extra = support::endian::read16be(Bytes.data() + 2);
        Size = 4;
        if (S == 0) {
          MI.setOpcode(TMS9900::DIVam);
          MI.addOperand(MCOperand::createImm(Extra));
        } else {
          MI.setOpcode(TMS9900::DIVxm);
          MI.addOperand(MCOperand::createImm(Extra));
          if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
            return MCDisassembler::Fail;
        }
        return MCDisassembler::Success;
      }
      return MCDisassembler::Fail;
    }
    case 0x0C: // LDCR
    case 0x0D: { // STCR
      bool IsLoad = (Op6 == 0x0C);
      unsigned Opcode = 0;
      if (Ts == 0)
        Opcode = IsLoad ? TMS9900::LDCRr : TMS9900::STCRr;
      else if (Ts == 1)
        Opcode = IsLoad ? TMS9900::LDCRi : TMS9900::STCRi;
      else if (Ts == 3)
        Opcode = IsLoad ? TMS9900::LDCRpim : TMS9900::STCRpim;
      else if (Ts == 2) {
        if (Bytes.size() < 4) return MCDisassembler::Fail;
        uint16_t Extra = support::endian::read16be(Bytes.data() + 2);
        Size = 4;
        if (S == 0) {
          MI.setOpcode(IsLoad ? TMS9900::LDCRam : TMS9900::STCRam);
          MI.addOperand(MCOperand::createImm(Extra));
        } else {
          MI.setOpcode(IsLoad ? TMS9900::LDCRxm : TMS9900::STCRxm);
          MI.addOperand(MCOperand::createImm(Extra));
          if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
            return MCDisassembler::Fail;
        }
        MI.addOperand(MCOperand::createImm(D));
        return MCDisassembler::Success;
      }
      if (Opcode == 0)
        return MCDisassembler::Fail;
      MI.setOpcode(Opcode);
      if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
        return MCDisassembler::Fail;
      MI.addOperand(MCOperand::createImm(D));
      return MCDisassembler::Success;
    }
    default:
      break;
    }
  }

  // Check Format 11: Single-word instructions
  if (Insn == RTWP_OPCODE) {
    MI.setOpcode(TMS9900::RTWP);
    return MCDisassembler::Success;
  }
  if (Insn == IDLE_OPCODE) {
    MI.setOpcode(TMS9900::IDLE);
    return MCDisassembler::Success;
  }
  if (Insn == RSET_OPCODE) {
    MI.setOpcode(TMS9900::RSET);
    return MCDisassembler::Success;
  }
  if (Insn == CKOF_OPCODE) {
    MI.setOpcode(TMS9900::CKOF);
    return MCDisassembler::Success;
  }
  if (Insn == CKON_OPCODE) {
    MI.setOpcode(TMS9900::CKON);
    return MCDisassembler::Success;
  }
  if (Insn == LREX_OPCODE) {
    MI.setOpcode(TMS9900::LREX);
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

    // Match by full opcode pattern (bits 15-6)
    // Opcode values from TMS9900InstrInfo.td:
    //   CLR  = 0x04C0 (0b0000010011 << 6)
    //   NEG  = 0x0500 (0b0000010100 << 6)
    //   INV  = 0x0540 (0b0000010101 << 6)
    //   INC  = 0x0580 (0b0000010110 << 6)
    //   INCT = 0x05C0 (0b0000010111 << 6)
    //   DEC  = 0x0600 (0b0000011000 << 6)
    //   DECT = 0x0640 (0b0000011001 << 6)
    //   BL   = 0x0680 (0b0000011010 << 6)
    //   SWPB = 0x06C0 (0b0000011011 << 6)
    //   SETO = 0x0700 (0b0000011100 << 6)
    //   ABS  = 0x0740 (0b0000011101 << 6)
    uint16_t Op6 = Insn & 0xFFC0;  // Opcode without Ts/S
    bool needsTiedOperand = false;  // For instructions with $rd = $rs constraint
    switch (Op6) {
    case 0x0400: Opcode = TMS9900::BLWP; break;
    case 0x0440: Opcode = TMS9900::Br; break;
    case 0x0480: Opcode = TMS9900::X; break;
    case 0x04C0: Opcode = TMS9900::CLRr; break;
    case 0x0500: Opcode = TMS9900::NEGr; needsTiedOperand = true; break;
    case 0x0540: Opcode = TMS9900::INVr; needsTiedOperand = true; break;
    case 0x0580: Opcode = TMS9900::INCr; needsTiedOperand = true; break;
    case 0x05C0: Opcode = TMS9900::INCTr; needsTiedOperand = true; break;
    case 0x0600: Opcode = TMS9900::DECr; needsTiedOperand = true; break;
    case 0x0640: Opcode = TMS9900::DECTr; needsTiedOperand = true; break;
    case 0x0680: Opcode = TMS9900::BL; break;
    case 0x06C0: Opcode = TMS9900::SWPBr; needsTiedOperand = true; break;
    case 0x0700: Opcode = TMS9900::SETOr; break;
    case 0x0740: Opcode = TMS9900::ABSr; needsTiedOperand = true; break;
    default:
      break;
    }

    if (Opcode == 0)
      return MCDisassembler::Fail;

    // BLWP and X support general addressing modes (incl. symbolic/indexed)
    if (Opcode == TMS9900::BLWP || Opcode == TMS9900::X) {
      bool IsBLWP = (Opcode == TMS9900::BLWP);
      if (Ts == 2) {
        if (Bytes.size() < 4) return MCDisassembler::Fail;
        uint16_t Extra = support::endian::read16be(Bytes.data() + 2);
        Size = 4;
        if (S == 0) {
          MI.setOpcode(IsBLWP ? TMS9900::BLWP_sym : TMS9900::X_sym);
          MI.addOperand(MCOperand::createImm(Extra));
        } else {
          MI.setOpcode(IsBLWP ? TMS9900::BLWP_idx : TMS9900::X_idx);
          MI.addOperand(MCOperand::createImm(Extra));
          if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
            return MCDisassembler::Fail;
        }
        return MCDisassembler::Success;
      }
      if (Ts == 3) {
        MI.setOpcode(IsBLWP ? TMS9900::BLWP_pim : TMS9900::X_pim);
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      if (Ts == 1) {
        MI.setOpcode(IsBLWP ? TMS9900::BLWP : TMS9900::X);
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      if (Ts == 0) {
        MI.setOpcode(IsBLWP ? TMS9900::BLWP_reg : TMS9900::X_reg);
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      return MCDisassembler::Fail;
    }

    // Special case: BL with symbolic addressing
    if (Opcode == TMS9900::BL && Ts == 2 && S == 0) {
      if (Bytes.size() < 4) return MCDisassembler::Fail;
      uint16_t Target = support::endian::read16be(Bytes.data() + 2);
      Size = 4;
      MI.setOpcode(TMS9900::BL_sym);
      MI.addOperand(MCOperand::createImm(Target));
      return MCDisassembler::Success;
    }

    // Special case: B (branch) with symbolic addressing
    if (Opcode == TMS9900::Br && Ts == 2 && S == 0) {
      if (Bytes.size() < 4) return MCDisassembler::Fail;
      uint16_t Target = support::endian::read16be(Bytes.data() + 2);
      Size = 4;
      MI.setOpcode(TMS9900::B_sym);
      MI.addOperand(MCOperand::createImm(Target));
      return MCDisassembler::Success;
    }

    // Select the correct opcode for B/BL based on addressing mode
    if (Opcode == TMS9900::Br) {
      if (Ts == 0)
        Opcode = TMS9900::Br_reg;
      else if (Ts != 1)
        return MCDisassembler::Fail;
    } else if (Opcode == TMS9900::BL) {
      if (Ts == 0)
        Opcode = TMS9900::BL_reg;
      else if (Ts != 1)
        return MCDisassembler::Fail;
    }

    auto mapSingleOperandOpcode = [&](unsigned Base) -> unsigned {
      switch (Base) {
      case TMS9900::CLRr:
        if (Ts == 1) return TMS9900::CLR_ind;
        if (Ts == 2 && S == 0) return TMS9900::CLR_sym;
        if (Ts == 2) return TMS9900::CLR_idx;
        if (Ts == 3) return TMS9900::CLR_pim;
        break;
      case TMS9900::SETOr:
        if (Ts == 1) return TMS9900::SETO_ind;
        if (Ts == 2 && S == 0) return TMS9900::SETO_sym;
        if (Ts == 2) return TMS9900::SETO_idx;
        if (Ts == 3) return TMS9900::SETO_pim;
        break;
      case TMS9900::SWPBr:
        if (Ts == 1) return TMS9900::SWPB_ind;
        if (Ts == 2 && S == 0) return TMS9900::SWPB_sym;
        if (Ts == 2) return TMS9900::SWPB_idx;
        if (Ts == 3) return TMS9900::SWPB_pim;
        break;
      case TMS9900::INCr:
        if (Ts == 1) return TMS9900::INC_ind;
        if (Ts == 2 && S == 0) return TMS9900::INC_sym;
        if (Ts == 2) return TMS9900::INC_idx;
        if (Ts == 3) return TMS9900::INC_pim;
        break;
      case TMS9900::INCTr:
        if (Ts == 1) return TMS9900::INCT_ind;
        if (Ts == 2 && S == 0) return TMS9900::INCT_sym;
        if (Ts == 2) return TMS9900::INCT_idx;
        if (Ts == 3) return TMS9900::INCT_pim;
        break;
      case TMS9900::DECr:
        if (Ts == 1) return TMS9900::DEC_ind;
        if (Ts == 2 && S == 0) return TMS9900::DEC_sym;
        if (Ts == 2) return TMS9900::DEC_idx;
        if (Ts == 3) return TMS9900::DEC_pim;
        break;
      case TMS9900::DECTr:
        if (Ts == 1) return TMS9900::DECT_ind;
        if (Ts == 2 && S == 0) return TMS9900::DECT_sym;
        if (Ts == 2) return TMS9900::DECT_idx;
        if (Ts == 3) return TMS9900::DECT_pim;
        break;
      case TMS9900::NEGr:
        if (Ts == 1) return TMS9900::NEG_ind;
        if (Ts == 2 && S == 0) return TMS9900::NEG_sym;
        if (Ts == 2) return TMS9900::NEG_idx;
        if (Ts == 3) return TMS9900::NEG_pim;
        break;
      case TMS9900::ABSr:
        if (Ts == 1) return TMS9900::ABS_ind;
        if (Ts == 2 && S == 0) return TMS9900::ABS_sym;
        if (Ts == 2) return TMS9900::ABS_idx;
        if (Ts == 3) return TMS9900::ABS_pim;
        break;
      case TMS9900::INVr:
        if (Ts == 1) return TMS9900::INV_ind;
        if (Ts == 2 && S == 0) return TMS9900::INV_sym;
        if (Ts == 2) return TMS9900::INV_idx;
        if (Ts == 3) return TMS9900::INV_pim;
        break;
      default:
        break;
      }
      return Base;
    };

    if (Ts != 0) {
      unsigned Mapped = mapSingleOperandOpcode(Opcode);
      if (Mapped == 0)
        return MCDisassembler::Fail;
      Opcode = Mapped;
    }

    MI.setOpcode(Opcode);

    // Handle addressing mode
    if (Ts == 0) {
      // Register direct
      // For instructions with tied constraints (outs $rd), (ins $rs) where $rd = $rs,
      // we need to add the register TWICE: once for output, once for input
      if (needsTiedOperand) {
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
      }
      if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
        return MCDisassembler::Fail;
    } else if (Ts == 1 || Ts == 3) {
      if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
        return MCDisassembler::Fail;
    } else if (Ts == 2 && S == 0) {
      // Symbolic addressing - need second word
      if (Bytes.size() < 4) return MCDisassembler::Fail;
      uint16_t Addr = support::endian::read16be(Bytes.data() + 2);
      Size = 4;
      MI.addOperand(MCOperand::createImm(Addr));
    } else if (Ts == 2) {
      if (Bytes.size() < 4) return MCDisassembler::Fail;
      uint16_t Offset = support::endian::read16be(Bytes.data() + 2);
      Size = 4;
      MI.addOperand(MCOperand::createImm(Offset));
      if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
        return MCDisassembler::Fail;
    } else {
      return MCDisassembler::Fail;
    }
    return MCDisassembler::Success;
  }

  // Check Format 1: Dual operand (high nibble 4-F)
  // Format: [opcode:4][Td:2][D:4][Ts:2][S:4]
  unsigned Op4 = (Insn >> 12) & 0xF;
  if (Op4 >= 4) {
    unsigned Td = (Insn >> 10) & 0x3;
    unsigned D = (Insn >> 6) & 0xF;
    unsigned Ts = (Insn >> 4) & 0x3;
    unsigned S = Insn & 0xF;

    // For now, only handle register-to-register operations (Ts=0, Td=0)
    // and register-to-indirect store (Ts=0, Td=1)
    // This covers the most common cases and avoids complex operand handling

    // Calculate instruction size based on addressing modes
    unsigned ExtraWords = 0;
    if (Ts == 2) ExtraWords++;  // Symbolic/indexed source
    if (Td == 2) ExtraWords++;  // Symbolic/indexed dest

    if (ExtraWords > 0 && Bytes.size() < 2 + ExtraWords * 2)
      return MCDisassembler::Fail;

    Size = 2 + ExtraWords * 2;

    // Handle register-to-register (Ts=0, Td=0) - most common case
    if (Ts == 0 && Td == 0) {
      unsigned Opcode = 0;
      bool needsTiedOperand = false;  // For A/S/SOC/SZC which have $rd = $rs1
      switch (Op4) {
      case 0x4: Opcode = TMS9900::SZCrr; needsTiedOperand = true; break;
      case 0x6: Opcode = TMS9900::Srr; needsTiedOperand = true; break;
      case 0x8: Opcode = TMS9900::Crr; break;
      case 0xA: Opcode = TMS9900::Arr; needsTiedOperand = true; break;
      case 0xC: Opcode = TMS9900::MOVrr; break;
      case 0xE: Opcode = TMS9900::SOCrr; needsTiedOperand = true; break;
      // Byte operations
      case 0x5: Opcode = TMS9900::SZCBrr; needsTiedOperand = true; break;
      case 0x7: Opcode = TMS9900::SBrr; needsTiedOperand = true; break;
      case 0x9: Opcode = TMS9900::CBrr; break;
      case 0xB: Opcode = TMS9900::ABrr; needsTiedOperand = true; break;
      case 0xD: Opcode = TMS9900::MOVBrr; break;
      case 0xF: Opcode = TMS9900::SOCBrr; needsTiedOperand = true; break;
      default:
        return MCDisassembler::Fail;
      }
      MI.setOpcode(Opcode);
      // For tied operands (A/S/SOC/SZC): (outs $rd), (ins $rs1, $rs2) with $rd = $rs1
      // Need to add dest register twice (output and first input)
      if (needsTiedOperand) {
        if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
      }
      // For compare: (outs), (ins $rs1, $rs2) - both are inputs
      if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
        return MCDisassembler::Fail;
      if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
        return MCDisassembler::Fail;
      return MCDisassembler::Success;
    }

    auto getFormat1LoadOpcode = [&](unsigned Op4, unsigned Mode) -> unsigned {
      switch (Op4) {
      case 0x4: return Mode == 0 ? TMS9900::SZCim : Mode == 1 ? TMS9900::SZCam
                              : Mode == 2 ? TMS9900::SZCxm : TMS9900::SZCpim;
      case 0x5: return Mode == 0 ? TMS9900::SZCBim : Mode == 1 ? TMS9900::SZCBam
                              : Mode == 2 ? TMS9900::SZCBxm : TMS9900::SZCBpim;
      case 0x6: return Mode == 0 ? TMS9900::Sim : Mode == 1 ? TMS9900::Sam
                              : Mode == 2 ? TMS9900::Sxm : TMS9900::Spim;
      case 0x7: return Mode == 0 ? TMS9900::SBim : Mode == 1 ? TMS9900::SBam
                              : Mode == 2 ? TMS9900::SBxm : TMS9900::SBpim;
      case 0x8: return Mode == 0 ? TMS9900::Cim : Mode == 1 ? TMS9900::Cam
                              : Mode == 2 ? TMS9900::Cxm : TMS9900::Cpim;
      case 0x9: return Mode == 0 ? TMS9900::CBim : Mode == 1 ? TMS9900::CBam
                              : Mode == 2 ? TMS9900::CBxm : TMS9900::CBpim;
      case 0xA: return Mode == 0 ? TMS9900::Aim : Mode == 1 ? TMS9900::Aam
                              : Mode == 2 ? TMS9900::Axm : TMS9900::Apim;
      case 0xB: return Mode == 0 ? TMS9900::ABim : Mode == 1 ? TMS9900::ABam
                              : Mode == 2 ? TMS9900::ABxm : TMS9900::ABpim;
      case 0xC: return Mode == 0 ? TMS9900::MOVim : Mode == 1 ? TMS9900::MOVam
                              : Mode == 2 ? TMS9900::MOVxm : TMS9900::MOVpim;
      case 0xD: return Mode == 0 ? TMS9900::MOVBim : Mode == 1 ? TMS9900::MOVBam
                              : Mode == 2 ? TMS9900::MOVBxm : TMS9900::MOVBpim;
      case 0xE: return Mode == 0 ? TMS9900::SOCim : Mode == 1 ? TMS9900::SOCam
                              : Mode == 2 ? TMS9900::SOCxm : TMS9900::SOCpim;
      case 0xF: return Mode == 0 ? TMS9900::SOCBim : Mode == 1 ? TMS9900::SOCBam
                              : Mode == 2 ? TMS9900::SOCBxm : TMS9900::SOCBpim;
      default:
        return 0;
      }
    };

    auto getFormat1StoreOpcode = [&](unsigned Op4, unsigned Mode) -> unsigned {
      switch (Op4) {
      case 0x4: return Mode == 0 ? TMS9900::SZCmi : Mode == 1 ? TMS9900::SZCma
                              : Mode == 2 ? TMS9900::SZCmx : TMS9900::SZCmpi;
      case 0x5: return Mode == 0 ? TMS9900::SZCBmi : Mode == 1 ? TMS9900::SZCBma
                              : Mode == 2 ? TMS9900::SZCBmx : TMS9900::SZCBmpi;
      case 0x6: return Mode == 0 ? TMS9900::Smi : Mode == 1 ? TMS9900::Sma
                              : Mode == 2 ? TMS9900::Smx : TMS9900::Smpi;
      case 0x7: return Mode == 0 ? TMS9900::SBmi : Mode == 1 ? TMS9900::SBma
                              : Mode == 2 ? TMS9900::SBmx : TMS9900::SBmpi;
      case 0x8: return Mode == 0 ? TMS9900::Cmi : Mode == 1 ? TMS9900::Cma
                              : Mode == 2 ? TMS9900::Cmx : TMS9900::Cmpi;
      case 0x9: return Mode == 0 ? TMS9900::CBmi : Mode == 1 ? TMS9900::CBma
                              : Mode == 2 ? TMS9900::CBmx : TMS9900::CBmpi;
      case 0xA: return Mode == 0 ? TMS9900::Ami : Mode == 1 ? TMS9900::Ama
                              : Mode == 2 ? TMS9900::Amx : TMS9900::Ampi;
      case 0xB: return Mode == 0 ? TMS9900::ABmi : Mode == 1 ? TMS9900::ABma
                              : Mode == 2 ? TMS9900::ABmx : TMS9900::ABmpi;
      case 0xC: return Mode == 0 ? TMS9900::MOVmi : Mode == 1 ? TMS9900::MOVma
                              : Mode == 2 ? TMS9900::MOVmx : TMS9900::MOVmpi;
      case 0xD: return Mode == 0 ? TMS9900::MOVBmi : Mode == 1 ? TMS9900::MOVBma
                              : Mode == 2 ? TMS9900::MOVBmx : TMS9900::MOVBmpi;
      case 0xE: return Mode == 0 ? TMS9900::SOCmi : Mode == 1 ? TMS9900::SOCma
                              : Mode == 2 ? TMS9900::SOCmx : TMS9900::SOCmpi;
      case 0xF: return Mode == 0 ? TMS9900::SOCBmi : Mode == 1 ? TMS9900::SOCBma
                              : Mode == 2 ? TMS9900::SOCBmx : TMS9900::SOCBmpi;
      default:
        return 0;
      }
    };

    // Source memory, dest register
    if (Td == 0 && Ts != 0) {
      unsigned Mode = Ts == 1 ? 0 : Ts == 2 ? (S == 0 ? 1 : 2) : 3;
      unsigned Opcode = getFormat1LoadOpcode(Op4, Mode);
      if (Opcode == 0)
        return MCDisassembler::Fail;
      MI.setOpcode(Opcode);
      if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
        return MCDisassembler::Fail;
      if (Ts == 1 || Ts == 3) {
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        if (Opcode == TMS9900::MOVpim || Opcode == TMS9900::MOVBpim) {
          if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
            return MCDisassembler::Fail;
        }
      } else if (Ts == 2 && S == 0) {
        uint16_t Addr = support::endian::read16be(Bytes.data() + 2);
        MI.addOperand(MCOperand::createImm(Addr));
      } else if (Ts == 2) {
        uint16_t Offset = support::endian::read16be(Bytes.data() + 2);
        MI.addOperand(MCOperand::createImm(Offset));
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
      } else {
        return MCDisassembler::Fail;
      }
      return MCDisassembler::Success;
    }

    // Dest memory, source register
    if (Ts == 0 && Td != 0) {
      unsigned Mode = Td == 1 ? 0 : Td == 2 ? (D == 0 ? 1 : 2) : 3;
      unsigned Opcode = getFormat1StoreOpcode(Op4, Mode);
      if (Opcode == 0)
        return MCDisassembler::Fail;
      MI.setOpcode(Opcode);
      if (Td == 1 || Td == 3) {
        if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        if (Opcode == TMS9900::MOVmpi || Opcode == TMS9900::MOVBmpi) {
          if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
            return MCDisassembler::Fail;
        }
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
      } else if (Td == 2 && D == 0) {
        uint16_t Addr = support::endian::read16be(Bytes.data() + 2);
        MI.addOperand(MCOperand::createImm(Addr));
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
      } else if (Td == 2) {
        uint16_t Offset = support::endian::read16be(Bytes.data() + 2);
        MI.addOperand(MCOperand::createImm(Offset));
        if (DecodeGR16RegisterClass(MI, D, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
        if (DecodeGR16RegisterClass(MI, S, Address, this) != MCDisassembler::Success)
          return MCDisassembler::Fail;
      } else {
        return MCDisassembler::Fail;
      }
      return MCDisassembler::Success;
    }

    // For other addressing mode combinations, return fail for now
    return MCDisassembler::Fail;
  }

  // Unknown instruction
  return MCDisassembler::Fail;
}
