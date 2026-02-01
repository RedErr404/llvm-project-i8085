//===-- TMS9900ELFObjectWriter.cpp - TMS9900 ELF Writer -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/TMS9900FixupKinds.h"
#include "MCTargetDesc/TMS9900MCTargetDesc.h"

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {

class TMS9900ELFObjectWriter : public MCELFObjectTargetWriter {
public:
  TMS9900ELFObjectWriter(uint8_t OSABI)
      : MCELFObjectTargetWriter(/*Is64Bit=*/false, OSABI, ELF::EM_TMS9900,
                                /*HasRelocationAddend=*/true) {}

  ~TMS9900ELFObjectWriter() override = default;

protected:
  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override {
    // Translate fixup kind to ELF relocation type.
    switch (Fixup.getTargetKind()) {
    case FK_Data_1:
      return ELF::R_TMS9900_NONE;  // 8-bit data, no relocation needed typically
    case FK_Data_2:
      return ELF::R_TMS9900_16;
    case FK_Data_4:
      return ELF::R_TMS9900_32;
    case TMS9900::fixup_tms9900_16:
      return ELF::R_TMS9900_16;
    case TMS9900::fixup_tms9900_8:
      return ELF::R_TMS9900_8;
    case TMS9900::fixup_tms9900_pcrel_8:
      return ELF::R_TMS9900_PCREL_8;
    case TMS9900::fixup_tms9900_pcrel_16:
      return ELF::R_TMS9900_PCREL_16;
    default:
      llvm_unreachable("Invalid fixup kind");
    }
  }
};

} // end of anonymous namespace

std::unique_ptr<MCObjectTargetWriter>
llvm::createTMS9900ELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<TMS9900ELFObjectWriter>(OSABI);
}
