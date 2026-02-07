//===--- TMS9900.cpp - TMS9900 Helpers for Tools ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TMS9900.h"
#include "CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Options.h"
#include "llvm/Option/ArgList.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

/// TMS9900 Toolchain
TMS9900ToolChain::TMS9900ToolChain(const Driver &D, const llvm::Triple &Triple,
                                   const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {
  // TMS9900 is a bare-metal target with no standard library paths by default
}

void TMS9900ToolChain::addClangTargetOptions(const ArgList &DriverArgs,
                                             ArgStringList &CC1Args,
                                             Action::OffloadKind) const {
  // Tail call optimization is safe on TMS9900. Although most instructions
  // set the status register, the CMPBR pseudo instruction fuses compare and
  // branch into a single unit that is only expanded after register allocation
  // and phi elimination, so phi-node copies cannot be inserted between compare
  // and branch instructions.
}
