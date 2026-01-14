//===-- TMS9900FixupKinds.h - TMS9900 Specific Fixup Entries ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_TMS9900_MCTARGETDESC_TMS9900FIXUPKINDS_H
#define LLVM_LIB_TARGET_TMS9900_MCTARGETDESC_TMS9900FIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace TMS9900 {

// This table must be in the same order as
// MCFixupKindInfo Infos[TMS9900::NumTargetFixupKinds]
// in TMS9900AsmBackend.cpp.
enum Fixups {
  // A 16 bit absolute fixup (for addresses in instruction words).
  fixup_tms9900_16 = FirstTargetFixupKind,

  // An 8 bit absolute fixup (for CRU bit displacements).
  fixup_tms9900_8,

  // A 8 bit PC relative fixup for jump instructions (Format 6).
  // The displacement is signed and in words (multiply by 2 for bytes).
  fixup_tms9900_pcrel_8,

  // A 16 bit PC relative fixup (rare, for extended branches).
  fixup_tms9900_pcrel_16,

  // Marker
  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};

} // end namespace TMS9900
} // end namespace llvm

#endif
