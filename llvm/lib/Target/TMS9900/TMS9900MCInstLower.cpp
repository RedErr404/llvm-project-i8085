//===-- TMS9900MCInstLower.cpp - Convert TMS9900 MachineInstr to MCInst ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains code to lower TMS9900 MachineInstrs to their
// corresponding MCInst records.
//
//===----------------------------------------------------------------------===//

#include "TMS9900MCInstLower.h"
#include "TMS9900.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

MCSymbol *
TMS9900MCInstLower::GetGlobalAddressSymbol(const MachineOperand &MO) const {
  return Printer.getSymbol(MO.getGlobal());
}

MCSymbol *
TMS9900MCInstLower::GetBlockAddressSymbol(const MachineOperand &MO) const {
  return Printer.GetBlockAddressSymbol(MO.getBlockAddress());
}

MCSymbol *
TMS9900MCInstLower::GetExternalSymbolSymbol(const MachineOperand &MO) const {
  return Printer.GetExternalSymbolSymbol(MO.getSymbolName());
}

MCSymbol *
TMS9900MCInstLower::GetJumpTableSymbol(const MachineOperand &MO) const {
  return Printer.GetJTISymbol(MO.getIndex());
}

MCSymbol *
TMS9900MCInstLower::GetConstantPoolIndexSymbol(const MachineOperand &MO) const {
  return Printer.GetCPISymbol(MO.getIndex());
}

MCOperand TMS9900MCInstLower::LowerSymbolOperand(const MachineOperand &MO,
                                                  MCSymbol *Sym) const {
  const MCExpr *Expr = MCSymbolRefExpr::create(Sym, Ctx);

  if (!MO.isJTI() && MO.getOffset())
    Expr = MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);

  return MCOperand::createExpr(Expr);
}

MCOperand TMS9900MCInstLower::LowerOperand(const MachineOperand &MO,
                                            unsigned Offset) const {
  switch (MO.getType()) {
  default:
    llvm_unreachable("unknown operand type");
  case MachineOperand::MO_Register:
    // Ignore all implicit register operands.
    if (MO.isImplicit())
      break;
    return MCOperand::createReg(MO.getReg());
  case MachineOperand::MO_Immediate:
    return MCOperand::createImm(MO.getImm() + Offset);
  case MachineOperand::MO_MachineBasicBlock:
    return MCOperand::createExpr(
        MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx));
  case MachineOperand::MO_GlobalAddress:
    return LowerSymbolOperand(MO, GetGlobalAddressSymbol(MO));
  case MachineOperand::MO_BlockAddress:
    return LowerSymbolOperand(MO, GetBlockAddressSymbol(MO));
  case MachineOperand::MO_ExternalSymbol:
    return LowerSymbolOperand(MO, GetExternalSymbolSymbol(MO));
  case MachineOperand::MO_JumpTableIndex:
    return LowerSymbolOperand(MO, GetJumpTableSymbol(MO));
  case MachineOperand::MO_ConstantPoolIndex:
    return LowerSymbolOperand(MO, GetConstantPoolIndexSymbol(MO));
  case MachineOperand::MO_RegisterMask:
    break;
  }

  return MCOperand();
}

void TMS9900MCInstLower::Lower(const MachineInstr *MI, MCInst &OutMI) const {
  unsigned Opc = MI->getOpcode();

  // Expand pseudo instructions to real instructions
  switch (Opc) {
  case TMS9900::MOV_FI_Store: {
    // MOV_FI_Store: (ins GR16:$base, i16imm:$offset, GR16:$rs)
    // -> MOVmx: (ins i16imm:$offset, GR16:$ri, GR16:$rs)
    OutMI.setOpcode(TMS9900::MOVmx);
    // Reorder operands: base(0), offset(1), rs(2) -> offset, base, rs
    OutMI.addOperand(LowerOperand(MI->getOperand(1))); // offset
    OutMI.addOperand(LowerOperand(MI->getOperand(0))); // base/ri
    OutMI.addOperand(LowerOperand(MI->getOperand(2))); // rs
    return;
  }
  case TMS9900::MOV_FI_Load: {
    // MOV_FI_Load: (outs GR16:$rd), (ins GR16:$base, i16imm:$offset)
    // -> MOVxm: (outs GR16:$rd), (ins i16imm:$offset, GR16:$ri)
    OutMI.setOpcode(TMS9900::MOVxm);
    // Reorder operands: rd(0), base(1), offset(2) -> rd, offset, base
    OutMI.addOperand(LowerOperand(MI->getOperand(0))); // rd
    OutMI.addOperand(LowerOperand(MI->getOperand(2))); // offset
    OutMI.addOperand(LowerOperand(MI->getOperand(1))); // base/ri
    return;
  }
  default:
    break;
  }

  // Default: pass through opcode and operands
  OutMI.setOpcode(Opc);

  for (const MachineOperand &MO : MI->operands()) {
    MCOperand MCOp = LowerOperand(MO);
    if (MCOp.isValid())
      OutMI.addOperand(MCOp);
  }
}
