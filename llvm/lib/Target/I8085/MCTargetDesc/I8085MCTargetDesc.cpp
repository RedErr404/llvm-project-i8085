//===-- I8085MCTargetDesc.cpp - I8085 Target Descriptions ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides I8085 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "I8085MCTargetDesc.h"
#include "I8085ELFStreamer.h"
#include "I8085InstPrinter.h"
#include "I8085MCAsmInfo.h"
#include "I8085MCELFStreamer.h"
#include "I8085TargetStreamer.h"
#include "TargetInfo/I8085TargetInfo.h"

#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/TargetRegistry.h"

#define GET_INSTRINFO_MC_DESC
#include "I8085GenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "I8085GenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "I8085GenRegisterInfo.inc"

using namespace llvm;

MCInstrInfo *llvm::createI8085MCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitI8085MCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createI8085MCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitI8085MCRegisterInfo(X, I8085::PC);
  return X;
}

static MCAsmInfo *createI8085MCAsmInfo(const MCRegisterInfo &MRI,
                                       const Triple &TT,
                                       const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new I8085MCAsmInfo(TT, Options);

  int StackGrowth = -2;
  MCCFIInstruction DefCfa = MCCFIInstruction::cfiDefCfa(
      nullptr, MRI.getDwarfRegNum(I8085::SP, true), -StackGrowth);
  MAI->addInitialFrameState(DefCfa);

  MCCFIInstruction RAOffset = MCCFIInstruction::createOffset(
      nullptr, MRI.getDwarfRegNum(I8085::PC, true), StackGrowth);
  MAI->addInitialFrameState(RAOffset);

  return MAI;
}

static MCSubtargetInfo *createI8085MCSubtargetInfo(const Triple &TT,
                                                 StringRef CPU, StringRef FS) {
  std::string FullFS = FS.str();
  std::string TripleStr = TT.str();
  if (TripleStr.find("+undoc") != std::string::npos && FullFS.find("+undoc") == std::string::npos)
    FullFS += "+undoc";
  return createI8085MCSubtargetInfoImpl(TT, CPU, /*TuneCPU*/ CPU, FullFS);
}

static MCInstPrinter *createI8085MCInstPrinter(const Triple &T,
                                             unsigned SyntaxVariant,
                                             const MCAsmInfo &MAI,
                                             const MCInstrInfo &MII,
                                             const MCRegisterInfo &MRI) {
  if (SyntaxVariant == 0) {
    return new I8085InstPrinter(MAI, MII, MRI);
  }

  return nullptr;
}

static MCStreamer *createMCStreamer(const Triple &T, MCContext &Context,
                                    std::unique_ptr<MCAsmBackend> &&MAB,
                                    std::unique_ptr<MCObjectWriter> &&OW,
                                    std::unique_ptr<MCCodeEmitter> &&Emitter) {
  return createELFStreamer(Context, std::move(MAB), std::move(OW),
                           std::move(Emitter));
}

static MCTargetStreamer *
createI8085ObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  return new I8085ELFStreamer(S, STI);
}

static MCTargetStreamer *createMCAsmTargetStreamer(MCStreamer &S,
                                                   formatted_raw_ostream &OS,
                                                   MCInstPrinter *InstPrint) {
  return new I8085TargetAsmStreamer(S);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeI8085TargetMC() {
  Target &T = getTheI8085Target();

  // Register the MC asm info.
  TargetRegistry::RegisterMCAsmInfo(T, createI8085MCAsmInfo);

  // Register the MC instruction info.
  TargetRegistry::RegisterMCInstrInfo(T, createI8085MCInstrInfo);

  // Register the MC register info.
  TargetRegistry::RegisterMCRegInfo(T, createI8085MCRegisterInfo);

  // Register the MC subtarget info.
  TargetRegistry::RegisterMCSubtargetInfo(T, createI8085MCSubtargetInfo);

  // Register the MCInstPrinter.
  TargetRegistry::RegisterMCInstPrinter(T, createI8085MCInstPrinter);

  // Register the MC Code Emitter
  TargetRegistry::RegisterMCCodeEmitter(T, createI8085MCCodeEmitter);

  // Register the obj streamer
  TargetRegistry::RegisterELFStreamer(T, createMCStreamer);

  // Register the obj target streamer.
  TargetRegistry::RegisterObjectTargetStreamer(T, createI8085ObjectTargetStreamer);

  // Register the asm target streamer.
  TargetRegistry::RegisterAsmTargetStreamer(T, createMCAsmTargetStreamer);

  // Register the asm backend (as little endian).
  TargetRegistry::RegisterMCAsmBackend(T, createI8085AsmBackend);
}
