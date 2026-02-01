//===- TMS9900.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The TMS9900 is a 16-bit big-endian microprocessor from Texas Instruments,
// introduced in 1976. It was used in the TI-99/4A home computer and various
// industrial applications. The architecture uses memory-mapped "workspace"
// registers and has a 64KB address space.
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "Target.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {

class TMS9900 final : public TargetInfo {
public:
  TMS9900();
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
};
} // namespace

TMS9900::TMS9900() {
  // BLWP @0 - trap instruction (branch to address 0, which is reset vector)
  // Opcode: 0x0400 followed by 0x0000
  trapInstr = {0x04, 0x00, 0x00, 0x00};
}

RelExpr TMS9900::getRelExpr(RelType type, const Symbol &s,
                            const uint8_t *loc) const {
  switch (type) {
  case R_TMS9900_PCREL_8:
  case R_TMS9900_PCREL_16:
    return R_PC;
  default:
    return R_ABS;
  }
}

void TMS9900::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  switch (rel.type) {
  case R_TMS9900_NONE:
    break;
  case R_TMS9900_8:
    checkIntUInt(loc, val, 8, rel);
    *loc = val;
    break;
  case R_TMS9900_16:
    checkIntUInt(loc, val, 16, rel);
    write16be(loc, val);
    break;
  case R_TMS9900_32:
    // 32-bit absolute (used by DWARF debug sections)
    write32be(loc, val);
    break;
  case R_TMS9900_PCREL_8: {
    // PC-relative 8-bit offset (used for JMP instructions)
    // The offset is in words, not bytes, and is relative to PC+2
    int64_t offset = (int64_t)val - 2;  // PC points to next instruction
    offset >>= 1;  // Convert byte offset to word offset
    checkInt(loc, offset, 8, rel);
    // The displacement is in the low byte of the instruction
    loc[1] = (uint8_t)offset;
    break;
  }
  case R_TMS9900_PCREL_16:
    checkIntUInt(loc, val, 16, rel);
    write16be(loc, val);
    break;
  default:
    error(getErrorLocation(loc) + "unrecognized relocation " +
          toString(rel.type));
  }
}

TargetInfo *elf::getTMS9900TargetInfo() {
  static TMS9900 target;
  return &target;
}
