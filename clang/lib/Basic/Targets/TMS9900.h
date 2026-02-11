//===--- TMS9900.h - Declare TMS9900 target feature support -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares TMS9900 TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_TMS9900_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_TMS9900_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY TMS9900TargetInfo : public TargetInfo {
  static const char *const GCCRegNames[];

public:
  TMS9900TargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    // TMS9900 has no TLS support
    TLSSupported = false;

    // Basic integer types
    IntWidth = 16;
    IntAlign = 16;
    LongWidth = 32;
    LongLongWidth = 64;
    LongAlign = LongLongAlign = 16;

    // Floating point (software emulation)
    FloatWidth = 32;
    FloatAlign = 16;
    DoubleWidth = LongDoubleWidth = 64;
    DoubleAlign = LongDoubleAlign = 16;

    // Pointers are 16-bit
    PointerWidth = 16;
    PointerAlign = 16;
    SuitableAlign = 16;

    // Type definitions
    SizeType = UnsignedInt;
    IntMaxType = SignedLongLong;
    IntPtrType = SignedInt;
    PtrDiffType = SignedInt;
    SigAtomicType = SignedLong;

    // TMS9900 is big-endian
    // Data layout must match the LLVM backend (TMS9900TargetMachine.cpp)
    // E = big endian, p:16:16 = 16-bit pointers
    // i8:8:8 = 8-bit ints with 8-bit alignment
    // i16:16:16 = 16-bit ints with 16-bit alignment
    // i32:16:32 = 32-bit ints with 16-bit ABI/32-bit preferred alignment
    // n16 = native integer width is 16 bits
    // S32 = 32-bit stack alignment (required: LLVM's type legalizer uses
    //   OR-instead-of-ADD for i32 split address computation, which needs
    //   4-byte-aligned base addresses)
    resetDataLayout("E-p:16:16-i8:8:8-i16:16:16-i32:16:32-n16-S32");
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  ArrayRef<Builtin::Info> getTargetBuiltins() const override {
    return std::nullopt;
  }

  bool allowsLargerPreferedTypeAlignment() const override { return false; }

  bool hasFeature(StringRef Feature) const override {
    return Feature == "tms9900";
  }

  ArrayRef<const char *> getGCCRegNames() const override;

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override {
    // TMS9900 register aliases
    // R10 = SP (stack pointer), R11 = LR (link register)
    static const TargetInfo::GCCRegAlias GCCRegAliases[] = {
        {{"sp"}, "r10"},
        {{"lr"}, "r11"},
    };
    return llvm::ArrayRef(GCCRegAliases);
  }

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &info) const override {
    // No target-specific constraints for now
    return false;
  }

  std::string_view getClobbers() const override {
    return "";
  }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::CharPtrBuiltinVaList;
  }
};

} // namespace targets
} // namespace clang
#endif // LLVM_CLANG_LIB_BASIC_TARGETS_TMS9900_H
