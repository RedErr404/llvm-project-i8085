//===-- I8085AsmPrinter.cpp - I8085 LLVM assembly writer ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to GAS-format I8085 assembly language.
//
//===----------------------------------------------------------------------===//

#include "I8085.h"
#include "I8085MCInstLower.h"
#include "I8085Subtarget.h"
#include "I8085TargetMachine.h"
#include "MCTargetDesc/I8085InstPrinter.h"
#include "MCTargetDesc/I8085MCExpr.h"
#include "TargetInfo/I8085TargetInfo.h"

#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Mangler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "i8085-asm-printer"

namespace llvm {

/// An I8085 assembly code printer.
class I8085AsmPrinter : public AsmPrinter {
public:
  I8085AsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)), MRI(*TM.getMCRegisterInfo()) {}

  StringRef getPassName() const override { return "I8085 Assembly Printer"; }

  void printOperand(const MachineInstr *MI, unsigned OpNo, raw_ostream &O);

  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNum,
                       const char *ExtraCode, raw_ostream &O) override;

  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNum,
                             const char *ExtraCode, raw_ostream &O) override;

  void emitInstruction(const MachineInstr *MI) override;

  const MCExpr *lowerConstant(const Constant *CV) override;

  void emitXXStructor(const DataLayout &DL, const Constant *CV) override;

  bool doFinalization(Module &M) override;

  void emitStartOfAsmFile(Module &M) override;

  void emitBasicBlockStart(const MachineBasicBlock &MBB) override;

private:
  const MCRegisterInfo &MRI;
};

void I8085AsmPrinter::emitBasicBlockStart(const MachineBasicBlock &MBB) {
  AsmPrinter::emitBasicBlockStart(MBB);
}

void I8085AsmPrinter::printOperand(const MachineInstr *MI, unsigned OpNo,
                                 raw_ostream &O) {
  const MachineOperand &MO = MI->getOperand(OpNo);
  LLVM_DEBUG(MO.dump());
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    O << I8085InstPrinter::getPrettyRegisterName(MO.getReg(), MRI);
    break;
  case MachineOperand::MO_Immediate:
    O << MO.getImm();
    break;
  case MachineOperand::MO_GlobalAddress:
    O << getSymbol(MO.getGlobal());
    break;
  case MachineOperand::MO_ExternalSymbol:
    O << *GetExternalSymbolSymbol(MO.getSymbolName());
    break;
  case MachineOperand::MO_MachineBasicBlock:
    O << *MO.getMBB()->getSymbol();
    break;
  default:
    llvm_unreachable("Not implemented yet!");
  }
}

bool I8085AsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNum,
                                    const char *ExtraCode, raw_ostream &O) {
  // Default asm printer can only deal with some extra codes,
  // so try it first.
  bool Error = AsmPrinter::PrintAsmOperand(MI, OpNum, ExtraCode, O);

  if (Error && ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1] != 0)
      return true; // Unknown modifier.

    if (ExtraCode[0] >= 'A' && ExtraCode[0] <= 'Z') {
      const MachineOperand &RegOp = MI->getOperand(OpNum);

      assert(RegOp.isReg() && "Operand must be a register when you're"
                              "using 'A'..'Z' operand extracodes.");
      Register Reg = RegOp.getReg();

      unsigned ByteNumber = ExtraCode[0] - 'A';
      const InlineAsm::Flag OpFlags(MI->getOperand(OpNum - 1).getImm());
      const unsigned NumOpRegs = OpFlags.getNumOperandRegisters();
      (void)NumOpRegs;

      const I8085Subtarget &STI = MF->getSubtarget<I8085Subtarget>();
      const TargetRegisterInfo &TRI = *STI.getRegisterInfo();

      const TargetRegisterClass *RC = TRI.getMinimalPhysRegClass(Reg);
      unsigned BytesPerReg = TRI.getRegSizeInBits(*RC) / 8;
      assert(BytesPerReg <= 2 && "Only 8 and 16 bit regs are supported.");

      unsigned RegIdx = ByteNumber / BytesPerReg;
      assert(RegIdx < NumOpRegs && "Multibyte index out of range.");

      Reg = MI->getOperand(OpNum + RegIdx).getReg();

      if (BytesPerReg == 2) {
        Reg = TRI.getSubReg(Reg, ByteNumber % BytesPerReg ? I8085::sub_hi
                                                          : I8085::sub_lo);
      }

      O << I8085InstPrinter::getPrettyRegisterName(Reg, MRI);
      return false;
    }
  }

  if (Error)
    printOperand(MI, OpNum, O);

  return false;
}

bool I8085AsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI,
                                          unsigned OpNum, const char *ExtraCode,
                                          raw_ostream &O) {
  if (ExtraCode && ExtraCode[0])
    return true; // Unknown modifier

  const MachineOperand &MO = MI->getOperand(OpNum);
  (void)MO;
  assert(MO.isReg() && "Unexpected inline asm memory operand");


  // If NumOpRegs == 2, then we assume it is product of a FrameIndex expansion
  // and the second operand is an Imm.
  const InlineAsm::Flag OpFlags(MI->getOperand(OpNum - 1).getImm());
  const unsigned NumOpRegs = OpFlags.getNumOperandRegisters();

  if (NumOpRegs == 2) {
    O << '+' << MI->getOperand(OpNum + 1).getImm();
  }

  return false;
}

void I8085AsmPrinter::emitInstruction(const MachineInstr *MI) {
  I8085MCInstLower MCInstLowering(OutContext, *this);

  // Indirect tail call: the target is in BC (it survives the HL-clobbering
  // epilogue). Move it into HL and jump via PCHL - no return address pushed.
  if (MI->getOpcode() == I8085::TCRETURN_INDIRECT) {
    auto emitRR = [&](unsigned Opc, unsigned R0, unsigned R1) {
      MCInst I;
      I.setOpcode(Opc);
      I.addOperand(MCOperand::createReg(R0));
      I.addOperand(MCOperand::createReg(R1));
      EmitToStreamer(*OutStreamer, I);
    };
    emitRR(I8085::MOV, I8085::H, I8085::B);
    emitRR(I8085::MOV, I8085::L, I8085::C);
    MCInst Jmp;
    Jmp.setOpcode(I8085::PCHL);
    EmitToStreamer(*OutStreamer, Jmp);
    return;
  }

  MCInst I;
  MCInstLowering.lowerInstruction(*MI, I);
  EmitToStreamer(*OutStreamer, I);
}

const MCExpr *I8085AsmPrinter::lowerConstant(const Constant *CV) {
  MCContext &Ctx = OutContext;

  if (const GlobalValue *GV = dyn_cast<GlobalValue>(CV)) {
    bool IsProgMem = GV->getAddressSpace() == I8085::ProgramMemory;
    if (IsProgMem) {
      const MCExpr *Expr = MCSymbolRefExpr::create(getSymbol(GV), Ctx);
      return I8085MCExpr::create(I8085MCExpr::VK_I8085_PM, Expr, false, Ctx);
    }
  }

  return AsmPrinter::lowerConstant(CV);
}

void I8085AsmPrinter::emitXXStructor(const DataLayout &DL, const Constant *CV) {
  AsmPrinter::emitXXStructor(DL, CV);
}

bool I8085AsmPrinter::doFinalization(Module &M) {

  return AsmPrinter::doFinalization(M);
}

void I8085AsmPrinter::emitStartOfAsmFile(Module &M) {

}

} // end of namespace llvm

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeI8085AsmPrinter() {
  llvm::RegisterAsmPrinter<llvm::I8085AsmPrinter> X(llvm::getTheI8085Target());
}
