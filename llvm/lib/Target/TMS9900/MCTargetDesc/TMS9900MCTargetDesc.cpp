//===-- TMS9900MCTargetDesc.cpp - TMS9900 Target Descriptions -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides TMS9900 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/TMS9900MCTargetDesc.h"
#include "MCTargetDesc/TMS9900FixupKinds.h"
#include "TargetInfo/TMS9900TargetInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Format.h"

using namespace llvm;

//===----------------------------------------------------------------------===//
// TMS9900 Assembly Dialect Selection
//===----------------------------------------------------------------------===//
// 0 = Default (LLVM-style): decimal immediates, .text/.data sections
// 1 = XAS99: >XXXX hex immediates, xas99-compatible output
//===----------------------------------------------------------------------===//

enum TMS9900AsmDialect { AD_Default = 0, AD_XAS99 = 1 };

static cl::opt<TMS9900AsmDialect> TMS9900AsmDialectOpt(
    "tms9900-asm-dialect", cl::init(AD_Default), cl::Hidden,
    cl::desc("Choose TMS9900 assembly dialect:"),
    cl::values(clEnumValN(AD_Default, "default",
                          "Default LLVM-style assembly"),
               clEnumValN(AD_XAS99, "xas99",
                          "xas99-compatible assembly (>XXXX hex)")));

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "TMS9900GenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "TMS9900GenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "TMS9900GenRegisterInfo.inc"

namespace {
//===----------------------------------------------------------------------===//
// TMS9900MCAsmInfo - xas99-compatible assembly syntax
//===----------------------------------------------------------------------===//
class TMS9900MCAsmInfo : public MCAsmInfo {
public:
  explicit TMS9900MCAsmInfo(const Triple &TT) {
    // Code pointer size is 16 bits (2 bytes)
    CodePointerSize = 2;
    CalleeSaveStackSlotSize = 2;

    // Set assembler dialect based on command-line option
    AssemblerDialect = TMS9900AsmDialectOpt;

    // TMS9900 is big-endian
    IsLittleEndian = false;

    // xas99 accepts ; for comments
    CommentString = ";";

    // DATA for 16-bit words, BYTE for 8-bit
    Data16bitsDirective = "\tDATA ";
    Data8bitsDirective = "\tBYTE ";
    Data32bitsDirective = nullptr;

    AsciiDirective = "\tTEXT '";
    AscizDirective = nullptr;

    // xas99 uses DEF to export symbols
    GlobalDirective = "\tDEF ";

    // xas99 doesn't support ELF directives
    HasDotTypeDotSizeDirective = false;
    HasSingleParameterDotFile = false;
    SupportsQuotedNames = false;

    PrivateGlobalPrefix = "L";
    PrivateLabelPrefix = "L";

    AlignmentIsInBytes = true;
    HasFunctionAlignment = false;

    UsesELFSectionDirectiveForBSS = false;

    if (TMS9900AsmDialectOpt == AD_XAS99) {
      ZeroDirective = "\tBSS ";
    }

    UseIntegratedAssembler = true;  // Enable AsmParser for .S files
    SupportsDebugInformation = true;
    UsesCFIWithoutEH = true;
    DwarfRegNumForCFI = true;
  }

  bool shouldOmitSectionDirective(StringRef SectionName) const override {
    // Only omit for standard code/data sections; debug sections need directives
    return MCAsmInfo::shouldOmitSectionDirective(SectionName);
  }
};

static MCAsmInfo *createTMS9900MCAsmInfo(const MCRegisterInfo &MRI,
                                          const Triple &TT,
                                          const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new TMS9900MCAsmInfo(TT);

  // Initial CFA state for the CIE: CFA = R10 (SP) + 0
  // On function entry, the stack pointer is the canonical frame address.
  unsigned DwarfSP = MRI.getDwarfRegNum(TMS9900::R10, true);
  MAI->addInitialFrameState(
      MCCFIInstruction::cfiDefCfa(nullptr, DwarfSP, 0));

  return MAI;
}

static MCInstrInfo *createTMS9900MCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitTMS9900MCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createTMS9900MCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitTMS9900MCRegisterInfo(X, TMS9900::R11);  // R11 is link register
  return X;
}

static MCSubtargetInfo *createTMS9900MCSubtargetInfo(const Triple &TT,
                                                      StringRef CPU,
                                                      StringRef FS) {
  return createTMS9900MCSubtargetInfoImpl(TT, CPU, /*TuneCPU*/ CPU, FS);
}

//===----------------------------------------------------------------------===//
// TMS9900InstPrinter
//===----------------------------------------------------------------------===//
class TMS9900InstPrinter : public MCInstPrinter {
public:
  TMS9900InstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                      const MCRegisterInfo &MRI)
      : MCInstPrinter(MAI, MII, MRI) {}

  void printInst(const MCInst *MI, uint64_t Address, StringRef Annot,
                 const MCSubtargetInfo &STI, raw_ostream &O) override {
    if (MI->getOpcode() == TMS9900::JMP && MI->getNumOperands() > 0) {
      const MCOperand &Op = MI->getOperand(0);
      if (Op.isImm() && Op.getImm() == static_cast<int64_t>(Address + 2)) {
        O << "NOP";
        printAnnotation(O, Annot);
        return;
      }
    }
    if (!printAliasInstr(MI, Address, O))
      printInstruction(MI, Address, O);
    printAnnotation(O, Annot);
  }

  void printInstruction(const MCInst *MI, uint64_t Address, raw_ostream &O);
  static const char *getRegisterName(MCRegister Reg);
  bool printAliasInstr(const MCInst *MI, uint64_t Address, raw_ostream &OS);
  void printCustomAliasOperand(const MCInst *MI, uint64_t Address,
                                unsigned OpIdx, unsigned PrintMethodIdx,
                                raw_ostream &OS);

  std::pair<const char *, uint64_t> getMnemonic(const MCInst *MI) override;

  void printOperand(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printBranchTarget16(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printRegName(raw_ostream &OS, MCRegister Reg) const override {
    OS << getRegisterName(Reg);
  }
};

void TMS9900InstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                       raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    O << getRegisterName(Op.getReg());
  } else if (Op.isImm()) {
    int64_t Imm = Op.getImm();
    if (MAI.getAssemblerDialect() == AD_XAS99) {
      uint16_t Val = static_cast<uint16_t>(Imm & 0xFFFF);
      O << ">" << format_hex_no_prefix(Val, 1);
    } else {
      O << Imm;
    }
  } else {
    assert(Op.isExpr() && "unknown operand kind in printOperand");
    Op.getExpr()->print(O, &MAI);
  }
}

void TMS9900InstPrinter::printBranchTarget16(const MCInst *MI, unsigned OpNo,
                                              raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isImm()) {
    // Always print branch targets in hex (addresses)
    uint16_t Addr = static_cast<uint16_t>(Op.getImm() & 0xFFFF);
    if (MAI.getAssemblerDialect() == AD_XAS99) {
      O << ">" << format_hex_no_prefix(Addr, 4);
    } else {
      O << "0x" << format_hex_no_prefix(Addr, 4);
    }
  } else if (Op.isExpr()) {
    Op.getExpr()->print(O, &MAI);
  } else {
    llvm_unreachable("Invalid branch target operand");
  }
}

// Include auto-generated printer
#define GET_INSTRUCTION_NAME
#define PRINT_ALIAS_INSTR
#include "TMS9900GenAsmWriter.inc"

static MCInstPrinter *createTMS9900MCInstPrinter(const Triple &T,
                                                  unsigned SyntaxVariant,
                                                  const MCAsmInfo &MAI,
                                                  const MCInstrInfo &MII,
                                                  const MCRegisterInfo &MRI) {
  return new TMS9900InstPrinter(MAI, MII, MRI);
}
} // end anonymous namespace

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeTMS9900TargetMC() {
  Target &T = getTheTMS9900Target();

  TargetRegistry::RegisterMCAsmInfo(T, createTMS9900MCAsmInfo);
  TargetRegistry::RegisterMCInstrInfo(T, createTMS9900MCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createTMS9900MCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createTMS9900MCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createTMS9900MCInstPrinter);
  TargetRegistry::RegisterMCCodeEmitter(T, createTMS9900MCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(T, createTMS9900MCAsmBackend);
}
