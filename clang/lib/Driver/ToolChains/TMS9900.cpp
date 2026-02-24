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
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

/// TMS9900 Toolchain
TMS9900ToolChain::TMS9900ToolChain(const Driver &D, const llvm::Triple &Triple,
                                   const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {
  // TMS9900 is a bare-metal target with no standard library paths by default.
  // Add the directory containing ld.lld so GetProgramPath can find it.
  getProgramPaths().push_back(D.Dir);
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

Tool *TMS9900ToolChain::buildLinker() const {
  return new tools::tms9900::Linker(*this);
}

/// TMS9900 Linker
void tms9900::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                   const InputInfo &Output,
                                   const InputInfoList &Inputs,
                                   const ArgList &Args,
                                   const char *LinkingOutput) const {
  const ToolChain &TC = getToolChain();
  const Driver &D = TC.getDriver();
  std::string LinkerPath = TC.GetProgramPath(getShortName());
  ArgStringList CmdArgs;

  // Forward linker scripts (-T)
  Args.AddAllArgs(CmdArgs, options::OPT_T);

  // Forward -L library paths
  Args.AddAllArgs(CmdArgs, options::OPT_L);
  TC.AddFilePathLibArgs(Args, CmdArgs);

  // Forward misc linker flags
  Args.addAllArgs(CmdArgs, {options::OPT_n, options::OPT_s, options::OPT_t,
                             options::OPT_u});

  // Add input files (.o, .a, etc.)
  AddLinkerInputs(TC, Inputs, Args, CmdArgs, JA);

  // Auto-link compiler-rt builtins (unless -nostdlib or -nodefaultlibs)
  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
    AddRunTimeLibs(TC, D, CmdArgs, Args);
  }

  // Output file
  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  C.addCommand(std::make_unique<Command>(
      JA, *this, ResponseFileSupport::AtFileCurCP(),
      Args.MakeArgString(LinkerPath), CmdArgs, Inputs, Output));
}
