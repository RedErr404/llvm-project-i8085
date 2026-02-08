//===-- TMS9900AsmPrinter.cpp - TMS9900 LLVM assembly writer --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to the TMS9900 assembly language.
//
//===----------------------------------------------------------------------===//

#include "TMS9900.h"
#include "TMS9900InstrInfo.h"
#include "TMS9900MCInstLower.h"
#include "TMS9900TargetMachine.h"
#include "TargetInfo/TMS9900TargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

namespace {

// Map MCRegister to printable name for the AsmPrinter.
static const char *getRegisterName(MCRegister Reg) {
  switch (Reg) {
  case TMS9900::R0: return "R0";
  case TMS9900::R1: return "R1";
  case TMS9900::R2: return "R2";
  case TMS9900::R3: return "R3";
  case TMS9900::R4: return "R4";
  case TMS9900::R5: return "R5";
  case TMS9900::R6: return "R6";
  case TMS9900::R7: return "R7";
  case TMS9900::R8: return "R8";
  case TMS9900::R9: return "R9";
  case TMS9900::R10: return "R10";
  case TMS9900::R11: return "R11";
  case TMS9900::R12: return "R12";
  case TMS9900::R13: return "R13";
  case TMS9900::R14: return "R14";
  case TMS9900::R15: return "R15";
  case TMS9900::PC: return "PC";
  case TMS9900::WP: return "WP";
  case TMS9900::ST: return "ST";
  default: return "?";
  }
}

class TMS9900AsmPrinter : public AsmPrinter {
public:
  TMS9900AsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)) {}

  StringRef getPassName() const override { return "TMS9900 Assembly Printer"; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void printOperand(const MachineInstr *MI, int OpNum, raw_ostream &O,
                    const char *Modifier = nullptr);
  void printMemRIOperand(const MachineInstr *MI, int OpNum, raw_ostream &O);
  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &O) override;
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &O) override;
  void emitInstruction(const MachineInstr *MI) override;
};
} // end of anonymous namespace

void TMS9900AsmPrinter::printOperand(const MachineInstr *MI, int OpNum,
                                      raw_ostream &O, const char *Modifier) {
  const MachineOperand &MO = MI->getOperand(OpNum);
  switch (MO.getType()) {
  default:
    llvm_unreachable("Not implemented yet!");
  case MachineOperand::MO_Register:
    O << getRegisterName(MO.getReg());
    return;
  case MachineOperand::MO_Immediate:
    O << MO.getImm();
    return;
  case MachineOperand::MO_MachineBasicBlock:
    MO.getMBB()->getSymbol()->print(O, MAI);
    return;
  case MachineOperand::MO_GlobalAddress:
    O << "@";
    getSymbol(MO.getGlobal())->print(O, MAI);
    if (MO.getOffset())
      O << "+" << MO.getOffset();
    return;
  case MachineOperand::MO_ExternalSymbol:
    O << "@";
    O << MO.getSymbolName();
    return;
  case MachineOperand::MO_ConstantPoolIndex:
    O << "@";
    O << MAI->getPrivateGlobalPrefix() << "CPI" << getFunctionNumber() << "_"
      << MO.getIndex();
    return;
  }
}

/// printMemRIOperand - Print a register+immediate memory operand for indexed
/// addressing: @offset(Rn)
void TMS9900AsmPrinter::printMemRIOperand(const MachineInstr *MI, int OpNum,
                                           raw_ostream &O) {
  const MachineOperand &Base = MI->getOperand(OpNum);
  const MachineOperand &Disp = MI->getOperand(OpNum + 1);

  O << "@";
  if (Disp.isImm()) {
    O << Disp.getImm();
  } else if (Disp.isGlobal()) {
    getSymbol(Disp.getGlobal())->print(O, MAI);
  } else {
    O << "0";  // Default offset
  }
  O << "(" << getRegisterName(Base.getReg()) << ")";
}

/// PrintAsmOperand - Print out an operand for an inline asm expression.
bool TMS9900AsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                         const char *ExtraCode,
                                         raw_ostream &O) {
  // Handle ExtraCode modifiers if needed
  if (ExtraCode && ExtraCode[0]) {
    switch (ExtraCode[0]) {
    default:
      return AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, O);
    case 'c': // Print as a plain immediate (no prefixes)
      if (MI->getOperand(OpNo).isImm()) {
        O << MI->getOperand(OpNo).getImm();
        return false;
      }
      return true;
    }
  }

  printOperand(MI, OpNo, O);
  return false;
}

/// PrintAsmMemoryOperand - Print a memory operand for inline asm.
/// For TMS9900, memory addressing can be:
///   @addr       - Symbolic
///   *Rn         - Register indirect
///   @offset(Rn) - Indexed
bool TMS9900AsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI,
                                               unsigned OpNo,
                                               const char *ExtraCode,
                                               raw_ostream &O) {
  if (ExtraCode && ExtraCode[0])
    return true; // Unknown modifier

  const MachineOperand &MO = MI->getOperand(OpNo);

  switch (MO.getType()) {
  default:
    return true; // Can't handle this type
  case MachineOperand::MO_Register:
    // Register indirect: *Rn
    O << "*" << getRegisterName(MO.getReg());
    return false;
  case MachineOperand::MO_GlobalAddress:
    // Symbolic: @symbol
    O << "@";
    getSymbol(MO.getGlobal())->print(O, MAI);
    if (MO.getOffset())
      O << "+" << MO.getOffset();
    return false;
  case MachineOperand::MO_ExternalSymbol:
    O << "@" << MO.getSymbolName();
    return false;
  }
}

void TMS9900AsmPrinter::emitInstruction(const MachineInstr *MI) {
  TMS9900MCInstLower MCInstLowering(OutContext, *this);

  MCInst TmpInst;
  MCInstLowering.Lower(MI, TmpInst);
  EmitToStreamer(*OutStreamer, TmpInst);
}

bool TMS9900AsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  SetupMachineFunction(MF);
  emitFunctionBody();
  return false;
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeTMS9900AsmPrinter() {
  RegisterAsmPrinter<TMS9900AsmPrinter> X(getTheTMS9900Target());
}
