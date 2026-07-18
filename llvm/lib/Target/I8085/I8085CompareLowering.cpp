//===-- I8085ISelLowering.cpp - I8085 DAG Lowering Implementation -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that I8085 uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "I8085ISelLowering.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/ErrorHandling.h"

#include <iostream>

#include "I8085.h"
#include "I8085MachineFunctionInfo.h"
#include "I8085Subtarget.h"
#include "I8085TargetMachine.h"
#include "MCTargetDesc/I8085MCTargetDesc.h"

namespace llvm {

static void addBranchToSingleSuccessor(MachineBasicBlock *MBB, const DebugLoc &DL,
                                       const I8085InstrInfo &TII) {
  if (MBB->succ_size() != 1)
    return;
  auto Last = MBB->getLastNonDebugInstr();
  if (Last != MBB->end() && Last->isTerminator())
    return;
  BuildMI(MBB, DL, TII.get(I8085::JMP)).addMBB(*MBB->succ_begin());
}

// Branchless materialization of an i8 unsigned/equality comparison into a 0/1
// boolean (destReg). After `MOV A,first ; SUB second`, CY = (first < second)
// unsigned; for EQ/NE a following `SUI 1` turns Z=(first==second) into CY (a-b
// borrows on subtracting 1 iff a-b==0). Then `SBB A ; ANI 1` yields CY as 0/1,
// and `SBB A ; INR A` yields !CY - so the whole select-diamond collapses to a
// short straight-line sequence with no branches or extra blocks. Mappings:
//   EQ  = a==b            NE  = a!=b (invert)
//   ULT = a<b             UGE = a>=b (invert)
//   UGT = ULT(b,a)        ULE = UGE(b,a)      (swap operands)
// All six formulas were verified exhaustively over the 65536 input pairs.
MachineBasicBlock *I8085TargetLowering::insertCond8Set(MachineInstr &MI,
                                                  MachineBasicBlock *MBB) const {
  int Opc = MI.getOpcode();
  const I8085InstrInfo &TII = (const I8085InstrInfo &)*MBB->getParent()
                                ->getSubtarget().getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();
  auto It = MachineBasicBlock::iterator(MI);

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned opOne = MI.getOperand(1).getReg();
  unsigned opTwo = MI.getOperand(2).getReg();

  unsigned first = opOne, second = opTwo;
  bool eqTest = false, invert = false;
  switch (Opc) {
  case I8085::SET_EQ_8:  eqTest = true; break;
  case I8085::SET_NE_8:  eqTest = true; invert = true; break;
  case I8085::SET_ULT_8: break;
  case I8085::SET_UGE_8: invert = true; break;
  case I8085::SET_UGT_8: std::swap(first, second); break;
  case I8085::SET_ULE_8: std::swap(first, second); invert = true; break;
  }

  BuildMI(*MBB, It, dl, TII.get(I8085::MOV))
      .addReg(I8085::A, RegState::Define).addReg(first);
  BuildMI(*MBB, It, dl, TII.get(I8085::SUB)).addReg(second);
  if (eqTest)
    BuildMI(*MBB, It, dl, TII.get(I8085::SUI)).addImm(1);
  BuildMI(*MBB, It, dl, TII.get(I8085::SBB)).addReg(I8085::A);
  if (invert)
    BuildMI(*MBB, It, dl, TII.get(I8085::INR))
        .addReg(I8085::A, RegState::Define).addReg(I8085::A);
  else
    BuildMI(*MBB, It, dl, TII.get(I8085::ANI)).addImm(1);
  BuildMI(*MBB, It, dl, TII.get(I8085::MOV))
      .addReg(destReg, RegState::Define).addReg(I8085::A);

  MI.eraseFromParent();
  return MBB;
}


// Fused signed i8 compare-and-branch. Emits, in-place (no new blocks):
//   MOV A,rhs ; XRI 0x80 ; MOV vRHS,A     ; biased RHS
//   MOV A,lhs ; XRI 0x80 ; SUB vRHS       ; A = biasedLHS - biasedRHS
//   Jcc target                            ; JC (SLT) / JNC (SGE)
// The XRI 0x80 bias turns a signed compare into an unsigned one:
//   signed(a) <cc> signed(b)  ==  unsigned(a^0x80) <cc> unsigned(b^0x80).
// vRHS is a virtual register (this runs pre-RA); its value is live across the
// `MOV A,lhs` that clobbers A, so the allocator cannot place it in A.
MachineBasicBlock *I8085TargetLowering::insertBrCCSigned8(MachineInstr &MI,
                                                  MachineBasicBlock *MBB) const {
  const I8085InstrInfo &TII = (const I8085InstrInfo &)*MBB->getParent()
                                ->getSubtarget().getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();
  MachineFunction *MF = MBB->getParent();
  auto It = MachineBasicBlock::iterator(MI);

  unsigned LHS = MI.getOperand(0).getReg();
  unsigned RHS = MI.getOperand(1).getReg();
  unsigned JmpOpc = (MI.getOpcode() == I8085::BR_CC_SLT_8) ? I8085::JC
                                                           : I8085::JNC;

  unsigned vRHS = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));

  BuildMI(*MBB, It, dl, TII.get(I8085::MOV))
      .addReg(I8085::A, RegState::Define).addReg(RHS);
  BuildMI(*MBB, It, dl, TII.get(I8085::XRI)).addImm(0x80);
  BuildMI(*MBB, It, dl, TII.get(I8085::MOV))
      .addReg(vRHS, RegState::Define).addReg(I8085::A);
  BuildMI(*MBB, It, dl, TII.get(I8085::MOV))
      .addReg(I8085::A, RegState::Define).addReg(LHS);
  BuildMI(*MBB, It, dl, TII.get(I8085::XRI)).addImm(0x80);
  BuildMI(*MBB, It, dl, TII.get(I8085::SUB)).addReg(vRHS);
  BuildMI(*MBB, It, dl, TII.get(JmpOpc)).add(MI.getOperand(2));

  MI.eraseFromParent();
  return MBB;
}

// Fused i16 compare-and-branch, all condition codes, emitted in-place (no new
// blocks). See BR_CC_*_16 in the .td for the per-condition sequence. Uses the
// SUB-then-SBB borrow for the 16-bit magnitude compare (MOV preserves CY so the
// low-byte borrow feeds the high-byte SBB), an XOR/OR combine for equality, and
// the high-byte sign bias (XRI 0x80) to reduce signed to unsigned. All temps are
// virtual registers (this runs pre-RA).
MachineBasicBlock *I8085TargetLowering::insertBrCC16(MachineInstr &MI,
                                                  MachineBasicBlock *MBB) const {
  const I8085InstrInfo &TII = (const I8085InstrInfo &)*MBB->getParent()
                                ->getSubtarget().getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();
  MachineFunction *MF = MBB->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  auto It = MachineBasicBlock::iterator(MI);
  unsigned Opc = MI.getOpcode();

  unsigned LHS = MI.getOperand(0).getReg();
  unsigned RHS = MI.getOperand(1).getReg();
  const TargetRegisterClass *RC8 = getRegClassFor(MVT::i8);

  auto Byte = [&](unsigned Pair, unsigned SubIdx) {
    unsigned V = MRI.createVirtualRegister(RC8);
    BuildMI(*MBB, It, dl, TII.get(TargetOpcode::COPY), V)
        .addReg(Pair, 0, SubIdx);
    return V;
  };
  unsigned lhsLo = Byte(LHS, I8085::sub_lo), lhsHi = Byte(LHS, I8085::sub_hi);
  unsigned rhsLo = Byte(RHS, I8085::sub_lo), rhsHi = Byte(RHS, I8085::sub_hi);

  auto MovA = [&](unsigned R) {
    BuildMI(*MBB, It, dl, TII.get(I8085::MOV))
        .addReg(I8085::A, RegState::Define).addReg(R);
  };
  auto Save = [&]() {
    unsigned V = MRI.createVirtualRegister(RC8);
    BuildMI(*MBB, It, dl, TII.get(I8085::MOV))
        .addReg(V, RegState::Define).addReg(I8085::A);
    return V;
  };
  auto Alu = [&](unsigned AluOpc, unsigned R) {
    BuildMI(*MBB, It, dl, TII.get(AluOpc)).addReg(R);
  };
  auto Xri = [&]() { BuildMI(*MBB, It, dl, TII.get(I8085::XRI)).addImm(0x80); };
  auto Jmp = [&](unsigned JOpc) {
    BuildMI(*MBB, It, dl, TII.get(JOpc)).add(MI.getOperand(2));
  };

  switch (Opc) {
  case I8085::BR_CC_EQ_16:
  case I8085::BR_CC_NE_16: {
    MovA(lhsLo); Alu(I8085::XRA, rhsLo);        // A = lo ^ lo
    unsigned t = Save();                        // t = low-byte difference
    MovA(lhsHi); Alu(I8085::XRA, rhsHi);        // A = hi ^ hi
    Alu(I8085::ORA, t);                         // Z iff both halves equal
    Jmp(Opc == I8085::BR_CC_EQ_16 ? I8085::JZ : I8085::JNZ);
    break;
  }
  case I8085::BR_CC_ULT_16:
  case I8085::BR_CC_UGE_16: {
    MovA(lhsLo); Alu(I8085::SUB, rhsLo);        // CY = low borrow
    MovA(lhsHi); Alu(I8085::SBB, rhsHi);        // CY = 16-bit borrow
    Jmp(Opc == I8085::BR_CC_ULT_16 ? I8085::JC : I8085::JNC);
    break;
  }
  case I8085::BR_CC_SLT_16:
  case I8085::BR_CC_SGE_16: {
    MovA(lhsHi); Xri(); unsigned ta = Save();   // ta = biased lhsHi
    MovA(rhsHi); Xri(); unsigned tb = Save();   // tb = biased rhsHi
    MovA(lhsLo); Alu(I8085::SUB, rhsLo);        // CY = low borrow
    MovA(ta);    Alu(I8085::SBB, tb);           // CY = signed compare
    Jmp(Opc == I8085::BR_CC_SLT_16 ? I8085::JC : I8085::JNC);
    break;
  }
  }

  MI.eraseFromParent();
  return MBB;
}

// Fused i16 compare-against-constant-and-branch, emitted in-place. Byte-level
// with immediate operands: SUI/SBI for magnitude (MOV preserves CY between the
// low SUI and the high SBI), XRI immLo/immHi + ORA for equality, and the
// high-byte 0x80 bias (on both the operand and the immediate) for signed.
MachineBasicBlock *I8085TargetLowering::insertBrCC16Imm(MachineInstr &MI,
                                                  MachineBasicBlock *MBB) const {
  const I8085InstrInfo &TII = (const I8085InstrInfo &)*MBB->getParent()
                                ->getSubtarget().getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();
  MachineFunction *MF = MBB->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  auto It = MachineBasicBlock::iterator(MI);
  unsigned Opc = MI.getOpcode();

  unsigned LHS = MI.getOperand(0).getReg();
  uint64_t Imm = (uint64_t)MI.getOperand(1).getImm() & 0xFFFF;
  unsigned ImmLo = Imm & 0xFF, ImmHi = (Imm >> 8) & 0xFF;
  const TargetRegisterClass *RC8 = getRegClassFor(MVT::i8);

  auto Byte = [&](unsigned SubIdx) {
    unsigned V = MRI.createVirtualRegister(RC8);
    BuildMI(*MBB, It, dl, TII.get(TargetOpcode::COPY), V).addReg(LHS, 0, SubIdx);
    return V;
  };
  unsigned lo = Byte(I8085::sub_lo), hi = Byte(I8085::sub_hi);

  auto MovA = [&](unsigned R) {
    BuildMI(*MBB, It, dl, TII.get(I8085::MOV))
        .addReg(I8085::A, RegState::Define).addReg(R);
  };
  auto ImmOp = [&](unsigned AluOpc, unsigned V) {
    BuildMI(*MBB, It, dl, TII.get(AluOpc)).addImm(V);
  };
  auto Save = [&]() {
    unsigned V = MRI.createVirtualRegister(RC8);
    BuildMI(*MBB, It, dl, TII.get(I8085::MOV))
        .addReg(V, RegState::Define).addReg(I8085::A);
    return V;
  };
  auto Jmp = [&](unsigned JOpc) {
    BuildMI(*MBB, It, dl, TII.get(JOpc)).add(MI.getOperand(2));
  };

  switch (Opc) {
  case I8085::BR_CC_EQ_16_IMM:
  case I8085::BR_CC_NE_16_IMM: {
    MovA(lo); ImmOp(I8085::XRI, ImmLo);          // A = lo ^ immLo
    unsigned t = Save();
    MovA(hi); ImmOp(I8085::XRI, ImmHi);          // A = hi ^ immHi
    BuildMI(*MBB, It, dl, TII.get(I8085::ORA)).addReg(t);
    Jmp(Opc == I8085::BR_CC_EQ_16_IMM ? I8085::JZ : I8085::JNZ);
    break;
  }
  case I8085::BR_CC_ULT_16_IMM:
  case I8085::BR_CC_UGE_16_IMM: {
    MovA(lo); ImmOp(I8085::SUI, ImmLo);          // CY = low borrow
    MovA(hi); ImmOp(I8085::SBI, ImmHi);          // CY = 16-bit borrow
    Jmp(Opc == I8085::BR_CC_ULT_16_IMM ? I8085::JC : I8085::JNC);
    break;
  }
  case I8085::BR_CC_SLT_16_IMM:
  case I8085::BR_CC_SGE_16_IMM: {
    if (Imm == 0) {
      // Sign test against 0: only the high byte's top bit matters.
      MovA(hi);
      BuildMI(*MBB, It, dl, TII.get(I8085::ORA)).addReg(I8085::A);
      Jmp(Opc == I8085::BR_CC_SLT_16_IMM ? I8085::JM : I8085::JP);
      break;
    }
    MovA(hi); ImmOp(I8085::XRI, 0x80);           // biased operand high byte
    unsigned tb = Save();
    MovA(lo); ImmOp(I8085::SUI, ImmLo);          // CY = low borrow
    MovA(tb); ImmOp(I8085::SBI, ImmHi ^ 0x80);   // CY = signed compare
    Jmp(Opc == I8085::BR_CC_SLT_16_IMM ? I8085::JC : I8085::JNC);
    break;
  }
  }

  MI.eraseFromParent();
  return MBB;
}

// Branchless materialization of an i8 SIGNED comparison into a 0/1 boolean.
// Same idea as insertCond8Set but the operands are first biased by XRI 0x80,
// which turns the signed compare into an unsigned one:
//   signed(a) <cc> signed(b)  ==  unsigned(a^0x80) <cc> unsigned(b^0x80).
// Then SUB gives CY = (first < second) signed, and SBB A;ANI 1 / SBB A;INR A
// convert CY / !CY into the boolean.  A temp holds the biased `second` across
// the `MOV A,first` that clobbers A. Replaces the same-sign/different-sign
// diamond entirely.
//   SLT = a<b            SGE = a>=b (invert)
//   SGT = SLT(b,a)       SLE = SGE(b,a)  (swap operands)
MachineBasicBlock *I8085TargetLowering::insertSigned8Cond(MachineInstr &MI,
                                                  MachineBasicBlock *MBB) const {
  int Opc = MI.getOpcode();
  const I8085InstrInfo &TII = (const I8085InstrInfo &)*MBB->getParent()
                                ->getSubtarget().getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  auto It = MachineBasicBlock::iterator(MI);

  unsigned destReg = MI.getOperand(0).getReg();
  unsigned first = MI.getOperand(1).getReg();
  unsigned second = MI.getOperand(2).getReg();
  bool invert = false;
  switch (Opc) {
  case I8085::SET_LT_8: break;
  case I8085::SET_GE_8: invert = true; break;
  case I8085::SET_GT_8: std::swap(first, second); break;
  case I8085::SET_LE_8: std::swap(first, second); invert = true; break;
  }

  unsigned vSecond = MRI.createVirtualRegister(getRegClassFor(MVT::i8));
  BuildMI(*MBB, It, dl, TII.get(I8085::MOV))
      .addReg(I8085::A, RegState::Define).addReg(second);
  BuildMI(*MBB, It, dl, TII.get(I8085::XRI)).addImm(0x80);
  BuildMI(*MBB, It, dl, TII.get(I8085::MOV))
      .addReg(vSecond, RegState::Define).addReg(I8085::A);
  BuildMI(*MBB, It, dl, TII.get(I8085::MOV))
      .addReg(I8085::A, RegState::Define).addReg(first);
  BuildMI(*MBB, It, dl, TII.get(I8085::XRI)).addImm(0x80);
  BuildMI(*MBB, It, dl, TII.get(I8085::SUB)).addReg(vSecond);
  BuildMI(*MBB, It, dl, TII.get(I8085::SBB)).addReg(I8085::A);
  if (invert)
    BuildMI(*MBB, It, dl, TII.get(I8085::INR))
        .addReg(I8085::A, RegState::Define).addReg(I8085::A);
  else
    BuildMI(*MBB, It, dl, TII.get(I8085::ANI)).addImm(1);
  BuildMI(*MBB, It, dl, TII.get(I8085::MOV))
      .addReg(destReg, RegState::Define).addReg(I8085::A);

  MI.eraseFromParent();
  return MBB;
}

MachineBasicBlock *I8085TargetLowering::insertDifferentSigned8Cond(MachineInstr &MI,
                                                  MachineBasicBlock *MBB) const {

  int Opc = MI.getOpcode();
  const I8085InstrInfo &TII = (const I8085InstrInfo &)*MI.getParent()
                                ->getParent()
                                ->getSubtarget()
                                .getInstrInfo();

  DebugLoc dl = MI.getDebugLoc();

  // To "insert" a SELECT instruction, we insert the diamond
  // control-flow pattern. The incoming instruction knows the
  // destination vreg to set, the condition code register to branch
  // on, the true/false values to select between, and a branch opcode
  // to use.

  MachineFunction *MF = MBB->getParent();
  
  const BasicBlock *LLVM_BB = MBB->getBasicBlock();
  MachineBasicBlock *FallThrough = MBB->getFallThrough();

  // If the current basic block falls through to another basic block,
  // we must insert an unconditional branch to the fallthrough destination
  // if we are to insert basic blocks at the prior fallthrough point.
  if (FallThrough != nullptr) {
    BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(FallThrough);
  }

  MachineBasicBlock *continMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *firstOperandPosMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *firstOperandNegMBB = MF->CreateMachineBasicBlock(LLVM_BB);

  MachineFunction::iterator I;
  for (I = MF->begin(); I != MF->end() && &(*I) != MBB; ++I)
    ;
  if (I != MF->end())
    ++I;
  MF->insert(I, continMBB);
  MF->insert(I, firstOperandPosMBB);
  MF->insert(I, firstOperandNegMBB);

  // Transfer remaining instructions and all successors of the current
  // block to the block which will contain the Phi node for the
  // select.
  continMBB->splice(continMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());

  continMBB->transferSuccessorsAndUpdatePHIs(MBB);
  addBranchToSingleSuccessor(continMBB, dl, TII);

  unsigned operandOne = MI.getOperand(1).getReg();
  
  
  unsigned tempRegOne = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));
  unsigned tempRegTwo = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));


  unsigned destReg = MI.getOperand(0).getReg();

  BuildMI(MBB, dl, TII.get(I8085::MOV))
        .addReg(I8085::A, RegState::Define)
        .addReg(operandOne);
  
  BuildMI(MBB, dl, TII.get(I8085::ANI))
        .addImm(128);   
  
  BuildMI(MBB, dl, TII.get(I8085::JZ))
        .addMBB(firstOperandPosMBB);

  BuildMI(MBB, dl, TII.get(I8085::JNZ))
        .addMBB(firstOperandNegMBB);        
  
  if(Opc == I8085::SET_DIFF_SIGN_GT_8 || Opc == I8085::SET_DIFF_SIGN_GE_8){
    BuildMI(firstOperandPosMBB, dl, TII.get(I8085::MVI)).addReg(tempRegOne, RegState::Define).addImm(1);
    BuildMI(firstOperandNegMBB, dl, TII.get(I8085::MVI)).addReg(tempRegTwo, RegState::Define).addImm(0);
  }

  else if(Opc == I8085::SET_DIFF_SIGN_LT_8 || Opc == I8085::SET_DIFF_SIGN_LE_8){
    BuildMI(firstOperandPosMBB, dl, TII.get(I8085::MVI)).addReg(tempRegOne, RegState::Define).addImm(0);
    BuildMI(firstOperandNegMBB, dl, TII.get(I8085::MVI)).addReg(tempRegTwo, RegState::Define).addImm(1);
  }

  BuildMI(firstOperandPosMBB, dl, TII.get(I8085::JMP)) .addMBB(continMBB);
  BuildMI(firstOperandNegMBB, dl, TII.get(I8085::JMP)) .addMBB(continMBB); 

  
  MBB->addSuccessor(firstOperandPosMBB);
  MBB->addSuccessor(firstOperandNegMBB);

  firstOperandPosMBB->addSuccessor(continMBB);
  firstOperandNegMBB->addSuccessor(continMBB);
  
  BuildMI(*continMBB, continMBB->begin(), dl, TII.get(I8085::PHI),destReg)
      .addReg(tempRegOne)
      .addMBB(firstOperandPosMBB)
      .addReg(tempRegTwo)
      .addMBB(firstOperandNegMBB);

  MI.eraseFromParent();
  return continMBB;
}


MachineBasicBlock *I8085TargetLowering::insertCond16Set(MachineInstr &MI,
                                                  MachineBasicBlock *MBB) const {

  int Opc = MI.getOpcode();
  const I8085InstrInfo &TII = (const I8085InstrInfo &)*MI.getParent()
                                ->getParent()
                                ->getSubtarget()
                                .getInstrInfo();

  DebugLoc dl = MI.getDebugLoc();

  // To "insert" a SELECT instruction, we insert the diamond
  // control-flow pattern. The incoming instruction knows the
  // destination vreg to set, the condition code register to branch
  // on, the true/false values to select between, and a branch opcode
  // to use.

  MachineFunction *MF = MBB->getParent();
  
  const BasicBlock *LLVM_BB = MBB->getBasicBlock();
  MachineBasicBlock *FallThrough = MBB->getFallThrough();

  // If the current basic block falls through to another basic block,
  // we must insert an unconditional branch to the fallthrough destination
  // if we are to insert basic blocks at the prior fallthrough point.
  if (FallThrough != nullptr) {
    BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(FallThrough);
  }

  MachineBasicBlock *trueMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *falseMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *continMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *checkMBB = nullptr;
  bool NeedsCarryCheck =
      (Opc == I8085::SET_UGT_16 || Opc == I8085::SET_ULE_16);
  if (NeedsCarryCheck)
    checkMBB = MF->CreateMachineBasicBlock(LLVM_BB);

  MachineFunction::iterator I;
  for (I = MF->begin(); I != MF->end() && &(*I) != MBB; ++I)
    ;
  if (I != MF->end())
    ++I;

  MF->insert(I, continMBB);
  MF->insert(I, trueMBB);
  MF->insert(I, falseMBB);
  if (checkMBB)
    MF->insert(I, checkMBB);
  

  // Transfer remaining instructions and all successors of the current
  // block to the block which will contain the Phi node for the
  // select.
  continMBB->splice(continMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());

  continMBB->transferSuccessorsAndUpdatePHIs(MBB);

  unsigned operandOne = MI.getOperand(1).getReg();
  unsigned operandTwo = MI.getOperand(2).getReg();
  
  
  unsigned tempRegOne =
      MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));
  unsigned tempRegTwo =
      MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));
  unsigned tempRegThree =
      MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i16));

  unsigned destReg = MI.getOperand(0).getReg();

  // Use a distinct destination for the subtraction so we don't create
  // multiple defs of the same vreg in SSA.
  BuildMI(MBB, dl, TII.get(I8085::SUB_16))
      .addReg(tempRegThree, RegState::Define)
      .addReg(operandOne)
      .addReg(operandTwo);

  if (Opc == I8085::SET_UGT_16) {
    BuildMI(MBB, dl, TII.get(I8085::JC)).addMBB(falseMBB);
    BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(checkMBB);

    MBB->addSuccessor(falseMBB);
    MBB->addSuccessor(checkMBB);

    BuildMI(checkMBB, dl, TII.get(I8085::JMP_16_IF_NOT_EQUAL))
        .addReg(operandOne)
        .addReg(operandTwo)
        .addMBB(trueMBB);
    BuildMI(checkMBB, dl, TII.get(I8085::JMP)).addMBB(falseMBB);

    checkMBB->addSuccessor(trueMBB);
    checkMBB->addSuccessor(falseMBB);
  } else if (Opc == I8085::SET_ULT_16) {
    BuildMI(MBB, dl, TII.get(I8085::JC)).addMBB(trueMBB);
    BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(falseMBB);

    MBB->addSuccessor(trueMBB);
    MBB->addSuccessor(falseMBB);
  } else if (Opc == I8085::SET_UGE_16) {
    BuildMI(MBB, dl, TII.get(I8085::JC)).addMBB(falseMBB);
    BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(trueMBB);

    MBB->addSuccessor(falseMBB);
    MBB->addSuccessor(trueMBB);
  } else if (Opc == I8085::SET_ULE_16) {
    BuildMI(MBB, dl, TII.get(I8085::JC)).addMBB(trueMBB);
    BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(checkMBB);

    MBB->addSuccessor(trueMBB);
    MBB->addSuccessor(checkMBB);

    BuildMI(checkMBB, dl, TII.get(I8085::JMP_16_IF_NOT_EQUAL))
        .addReg(operandOne)
        .addReg(operandTwo)
        .addMBB(falseMBB);
    BuildMI(checkMBB, dl, TII.get(I8085::JMP)).addMBB(trueMBB);

    checkMBB->addSuccessor(falseMBB);
    checkMBB->addSuccessor(trueMBB);
  }
  
  BuildMI(trueMBB, dl, TII.get(I8085::MVI))
        .addReg(tempRegOne, RegState::Define)
        .addImm(1);

  BuildMI(trueMBB, dl, TII.get(I8085::JMP))
            .addMBB(continMBB);


  // Unconditionally flow back to the true block
  BuildMI(falseMBB, dl, TII.get(I8085::MVI))
        .addReg(tempRegTwo, RegState::Define)
        .addImm(0);

  BuildMI(falseMBB, dl, TII.get(I8085::JMP))
            .addMBB(continMBB);

  falseMBB->addSuccessor(continMBB);
  trueMBB->addSuccessor(continMBB);
  
  BuildMI(*continMBB, continMBB->begin(), dl, TII.get(I8085::PHI),destReg)
      .addReg(tempRegOne)
      .addMBB(trueMBB)
      .addReg(tempRegTwo)
      .addMBB(falseMBB);

  MI.eraseFromParent();
  return continMBB;
}


MachineBasicBlock *I8085TargetLowering::insertEqualityCond16Set(MachineInstr &MI,
                                                  MachineBasicBlock *MBB) const {

  int Opc = MI.getOpcode();
  const I8085InstrInfo &TII = (const I8085InstrInfo &)*MI.getParent()
                                ->getParent()
                                ->getSubtarget()
                                .getInstrInfo();

  DebugLoc dl = MI.getDebugLoc();

  // To "insert" a SELECT instruction, we insert the diamond
  // control-flow pattern. The incoming instruction knows the
  // destination vreg to set, the condition code register to branch
  // on, the true/false values to select between, and a branch opcode
  // to use.

  MachineFunction *MF = MBB->getParent();
  
  const BasicBlock *LLVM_BB = MBB->getBasicBlock();
  MachineBasicBlock *FallThrough = MBB->getFallThrough();

  // If the current basic block falls through to another basic block,
  // we must insert an unconditional branch to the fallthrough destination
  // if we are to insert basic blocks at the prior fallthrough point.
  if (FallThrough != nullptr) {
    BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(FallThrough);
  }

  MachineBasicBlock *trueMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *falseMBB = MF->CreateMachineBasicBlock(LLVM_BB);

  MachineFunction::iterator I;
  for (I = MF->begin(); I != MF->end() && &(*I) != MBB; ++I)
    ;
  if (I != MF->end())
    ++I;
  MF->insert(I, trueMBB);
  MF->insert(I, falseMBB);

  // Transfer remaining instructions and all successors of the current
  // block to the block which will contain the Phi node for the
  // select.
  trueMBB->splice(trueMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());

  trueMBB->transferSuccessorsAndUpdatePHIs(MBB);

  unsigned operandOne = MI.getOperand(1).getReg();
  unsigned operandTwo = MI.getOperand(2).getReg();
  
  
  unsigned tempRegOne = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));
  unsigned tempRegTwo = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));


  unsigned destReg = MI.getOperand(0).getReg();

  BuildMI(MBB, dl, TII.get(I8085::MVI))
        .addReg(tempRegOne, RegState::Define)
        .addImm(1);      

  if(Opc == I8085::SET_EQ_16){
      BuildMI(MBB, dl, TII.get(I8085::JMP_16_IF_NOT_EQUAL))
        .addReg(operandOne)
        .addReg(operandTwo)
        .addMBB(falseMBB); 
      BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(trueMBB);   
  }

  else if(Opc == I8085::SET_NE_16){
      BuildMI(MBB, dl, TII.get(I8085::JMP_16_IF_NOT_EQUAL))
        .addReg(operandOne)
        .addReg(operandTwo)
        .addMBB(trueMBB);
      BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(falseMBB); 
  }

  
  MBB->addSuccessor(falseMBB);
  MBB->addSuccessor(trueMBB);

  // Unconditionally flow back to the true block
  BuildMI(falseMBB, dl, TII.get(I8085::MVI))
        .addReg(tempRegTwo, RegState::Define)
        .addImm(0);

  BuildMI(falseMBB, dl, TII.get(I8085::JMP))
            .addMBB(trueMBB);

  falseMBB->addSuccessor(trueMBB);
  
  BuildMI(*trueMBB, trueMBB->begin(), dl, TII.get(I8085::PHI),destReg)
      .addReg(tempRegOne)
      .addMBB(MBB)
      .addReg(tempRegTwo)
      .addMBB(falseMBB);

  MI.eraseFromParent();
  return trueMBB;
}


MachineBasicBlock *I8085TargetLowering::insertSignedCond16Set(MachineInstr &MI,
                                                  MachineBasicBlock *MBB) const {

  int Opc = MI.getOpcode();
  const I8085InstrInfo &TII = (const I8085InstrInfo &)*MI.getParent()
                                ->getParent()
                                ->getSubtarget()
                                .getInstrInfo();

  DebugLoc dl = MI.getDebugLoc();

  // To "insert" a SELECT instruction, we insert the diamond
  // control-flow pattern. The incoming instruction knows the
  // destination vreg to set, the condition code register to branch
  // on, the true/false values to select between, and a branch opcode
  // to use.

  MachineFunction *MF = MBB->getParent();
  
  const BasicBlock *LLVM_BB = MBB->getBasicBlock();
  MachineBasicBlock *FallThrough = MBB->getFallThrough();

  // If the current basic block falls through to another basic block,
  // we must insert an unconditional branch to the fallthrough destination
  // if we are to insert basic blocks at the prior fallthrough point.
  if (FallThrough != nullptr) {
    BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(FallThrough);
  }

  MachineBasicBlock *continMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *samesignMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *diffsignMBB = MF->CreateMachineBasicBlock(LLVM_BB);

  MachineFunction::iterator I;
  for (I = MF->begin(); I != MF->end() && &(*I) != MBB; ++I)
    ;
  if (I != MF->end())
    ++I;
  MF->insert(I, continMBB);
  MF->insert(I, samesignMBB);
  MF->insert(I, diffsignMBB);

  // Transfer remaining instructions and all successors of the current
  // block to the block which will contain the Phi node for the
  // select.
  continMBB->splice(continMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());

  continMBB->transferSuccessorsAndUpdatePHIs(MBB);

  unsigned operandOne = MI.getOperand(1).getReg();
  unsigned operandTwo = MI.getOperand(2).getReg();
  
  
  unsigned tempRegOne = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));
  unsigned tempRegTwo = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));

  unsigned destReg = MI.getOperand(0).getReg();

  BuildMI(MBB, dl, TII.get(I8085::JMP_16_IF_SAME_SIGN))
        .addReg(operandOne)
        .addReg(operandTwo)
        .addMBB(samesignMBB);

  BuildMI(MBB, dl, TII.get(I8085::JMP))
        .addMBB(diffsignMBB);      


  if(Opc == I8085::SET_GT_16){
    BuildMI(samesignMBB, dl, TII.get(I8085::SET_UGT_16)).addReg(tempRegOne,RegState::Define).addReg(operandOne).addReg(operandTwo);
    BuildMI(diffsignMBB, dl, TII.get(I8085::SET_DIFF_SIGN_GT_16)).addReg(tempRegTwo,RegState::Define).addReg(operandOne).addReg(operandTwo); 
  }

  else if(Opc == I8085::SET_LT_16){
    BuildMI(samesignMBB, dl, TII.get(I8085::SET_ULT_16)).addReg(tempRegOne,RegState::Define).addReg(operandOne).addReg(operandTwo);
    BuildMI(diffsignMBB, dl, TII.get(I8085::SET_DIFF_SIGN_LT_16)).addReg(tempRegTwo,RegState::Define).addReg(operandOne).addReg(operandTwo);  
  }

  else if(Opc == I8085::SET_GE_16){
    BuildMI(samesignMBB, dl, TII.get(I8085::SET_UGE_16)).addReg(tempRegOne,RegState::Define).addReg(operandOne).addReg(operandTwo);
    BuildMI(diffsignMBB, dl, TII.get(I8085::SET_DIFF_SIGN_LT_16)).addReg(tempRegTwo,RegState::Define).addReg(operandTwo).addReg(operandOne);  
  }

  else if(Opc == I8085::SET_LE_16){
    BuildMI(samesignMBB, dl, TII.get(I8085::SET_ULE_16)).addReg(tempRegOne,RegState::Define).addReg(operandOne).addReg(operandTwo); 
    BuildMI(diffsignMBB, dl, TII.get(I8085::SET_DIFF_SIGN_GT_16)).addReg(tempRegTwo,RegState::Define).addReg(operandTwo).addReg(operandOne);
  }
  
  BuildMI(samesignMBB, dl, TII.get(I8085::JMP)) .addMBB(continMBB);
  BuildMI(diffsignMBB, dl, TII.get(I8085::JMP)) .addMBB(continMBB); 
  
  MBB->addSuccessor(samesignMBB);
  MBB->addSuccessor(diffsignMBB);

  samesignMBB->addSuccessor(continMBB);
  diffsignMBB->addSuccessor(continMBB);

  
  BuildMI(*continMBB, continMBB->begin(), dl, TII.get(I8085::PHI),destReg)
      .addReg(tempRegOne)
      .addMBB(samesignMBB)
      .addReg(tempRegTwo)
      .addMBB(diffsignMBB);

  MI.eraseFromParent();
  return continMBB;
}



MachineBasicBlock *I8085TargetLowering::insertDifferentSignedCond16Set(MachineInstr &MI,
                                                  MachineBasicBlock *MBB) const {

  int Opc = MI.getOpcode();
  const I8085InstrInfo &TII = (const I8085InstrInfo &)*MI.getParent()
                                ->getParent()
                                ->getSubtarget()
                                .getInstrInfo();

  DebugLoc dl = MI.getDebugLoc();

  // To "insert" a SELECT instruction, we insert the diamond
  // control-flow pattern. The incoming instruction knows the
  // destination vreg to set, the condition code register to branch
  // on, the true/false values to select between, and a branch opcode
  // to use.

  MachineFunction *MF = MBB->getParent();
  
  const BasicBlock *LLVM_BB = MBB->getBasicBlock();
  MachineBasicBlock *FallThrough = MBB->getFallThrough();

  // If the current basic block falls through to another basic block,
  // we must insert an unconditional branch to the fallthrough destination
  // if we are to insert basic blocks at the prior fallthrough point.
  if (FallThrough != nullptr) {
    BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(FallThrough);
  }

  MachineBasicBlock *continMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *firstOperandPos = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *firstOperandNeg = MF->CreateMachineBasicBlock(LLVM_BB);

  MachineFunction::iterator I;
  for (I = MF->begin(); I != MF->end() && &(*I) != MBB; ++I)
    ;
  if (I != MF->end())
    ++I;
  MF->insert(I, continMBB);
  MF->insert(I, firstOperandPos);
  MF->insert(I, firstOperandNeg);

  // Transfer remaining instructions and all successors of the current
  // block to the block which will contain the Phi node for the
  // select.
  continMBB->splice(continMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());

  continMBB->transferSuccessorsAndUpdatePHIs(MBB);
  addBranchToSingleSuccessor(continMBB, dl, TII);

  unsigned operandOne = MI.getOperand(1).getReg();
  
  unsigned tempRegOne = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));
  unsigned tempRegTwo = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));

  unsigned destReg = MI.getOperand(0).getReg();

  BuildMI(MBB, dl, TII.get(I8085::JMP_16_IF_POSITIVE))
        .addReg(operandOne)
        .addMBB(firstOperandPos);

  BuildMI(MBB, dl, TII.get(I8085::JMP))
        .addMBB(firstOperandNeg);      


  if(Opc == I8085::SET_DIFF_SIGN_LT_16){
    BuildMI(firstOperandPos, dl, TII.get(I8085::MVI)).addReg(tempRegOne,RegState::Define).addImm(0);
    BuildMI(firstOperandNeg, dl, TII.get(I8085::MVI)).addReg(tempRegTwo,RegState::Define).addImm(1);
  }

  else if(Opc == I8085::SET_DIFF_SIGN_GT_16){
    BuildMI(firstOperandPos, dl, TII.get(I8085::MVI)).addReg(tempRegOne,RegState::Define).addImm(1);
    BuildMI(firstOperandNeg, dl, TII.get(I8085::MVI)).addReg(tempRegTwo,RegState::Define).addImm(0); 
  }
  
  BuildMI(firstOperandPos, dl, TII.get(I8085::JMP)) .addMBB(continMBB);
  BuildMI(firstOperandNeg, dl, TII.get(I8085::JMP)) .addMBB(continMBB); 
  
  MBB->addSuccessor(firstOperandPos);
  MBB->addSuccessor(firstOperandNeg);

  firstOperandPos->addSuccessor(continMBB);
  firstOperandNeg->addSuccessor(continMBB);

  
  BuildMI(*continMBB, continMBB->begin(), dl, TII.get(I8085::PHI),destReg)
      .addReg(tempRegOne)
      .addMBB(firstOperandPos)
      .addReg(tempRegTwo)
      .addMBB(firstOperandNeg);

  MI.eraseFromParent();
  return continMBB;
}

MachineBasicBlock *I8085TargetLowering::insertEqualityCond32Set(MachineInstr &MI,
                                                  MachineBasicBlock *MBB) const {

  int Opc = MI.getOpcode();
  const I8085InstrInfo &TII = (const I8085InstrInfo &)*MI.getParent()
                                ->getParent()
                                ->getSubtarget()
                                .getInstrInfo();

  DebugLoc dl = MI.getDebugLoc();

  // To "insert" a SELECT instruction, we insert the diamond
  // control-flow pattern. The incoming instruction knows the
  // destination vreg to set, the condition code register to branch
  // on, the true/false values to select between, and a branch opcode
  // to use.

  MachineFunction *MF = MBB->getParent();
  
  const BasicBlock *LLVM_BB = MBB->getBasicBlock();
  MachineBasicBlock *FallThrough = MBB->getFallThrough();

  // If the current basic block falls through to another basic block,
  // we must insert an unconditional branch to the fallthrough destination
  // if we are to insert basic blocks at the prior fallthrough point.
  if (FallThrough != nullptr) {
    BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(FallThrough);
  }

  MachineBasicBlock *trueMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *falseMBB = MF->CreateMachineBasicBlock(LLVM_BB);

  MachineFunction::iterator I;
  for (I = MF->begin(); I != MF->end() && &(*I) != MBB; ++I)
    ;
  if (I != MF->end())
    ++I;
  MF->insert(I, trueMBB);
  MF->insert(I, falseMBB);

  // Transfer remaining instructions and all successors of the current
  // block to the block which will contain the Phi node for the
  // select.
  trueMBB->splice(trueMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());

  trueMBB->transferSuccessorsAndUpdatePHIs(MBB);

  unsigned operandOne = MI.getOperand(1).getReg();
  unsigned operandTwo = MI.getOperand(2).getReg();
  
  
  unsigned tempRegOne = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));
  unsigned tempRegTwo = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));


  unsigned destReg = MI.getOperand(0).getReg();

  BuildMI(MBB, dl, TII.get(I8085::MVI))
        .addReg(tempRegOne, RegState::Define)
        .addImm(1);      

  if(Opc == I8085::SET_EQ_32){
      BuildMI(MBB, dl, TII.get(I8085::JMP_32_IF_NOT_EQUAL))
        .addReg(operandOne)
        .addReg(operandTwo)
        .addMBB(falseMBB); 
      BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(trueMBB);   
  }

  else if(Opc == I8085::SET_NE_32){
      BuildMI(MBB, dl, TII.get(I8085::JMP_32_IF_NOT_EQUAL))
        .addReg(operandOne)
        .addReg(operandTwo)
        .addMBB(trueMBB);
      BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(falseMBB); 
  }

  
  MBB->addSuccessor(falseMBB);
  MBB->addSuccessor(trueMBB);

  // Unconditionally flow back to the true block
  BuildMI(falseMBB, dl, TII.get(I8085::MVI))
        .addReg(tempRegTwo, RegState::Define)
        .addImm(0);

  BuildMI(falseMBB, dl, TII.get(I8085::JMP))
            .addMBB(trueMBB);

  falseMBB->addSuccessor(trueMBB);
  
  BuildMI(*trueMBB, trueMBB->begin(), dl, TII.get(I8085::PHI),destReg)
      .addReg(tempRegOne)
      .addMBB(MBB)
      .addReg(tempRegTwo)
      .addMBB(falseMBB);

  MI.eraseFromParent();
  return trueMBB;
}

MachineBasicBlock *I8085TargetLowering::insertCond32Set(MachineInstr &MI,
                                                  MachineBasicBlock *MBB) const {

  int Opc = MI.getOpcode();
  const I8085InstrInfo &TII = (const I8085InstrInfo &)*MI.getParent()
                                ->getParent()
                                ->getSubtarget()
                                .getInstrInfo();

  DebugLoc dl = MI.getDebugLoc();

  // To "insert" a SELECT instruction, we insert the diamond
  // control-flow pattern. The incoming instruction knows the
  // destination vreg to set, the condition code register to branch
  // on, the true/false values to select between, and a branch opcode
  // to use.

  MachineFunction *MF = MBB->getParent();
  
  const BasicBlock *LLVM_BB = MBB->getBasicBlock();
  MachineBasicBlock *FallThrough = MBB->getFallThrough();

  // If the current basic block falls through to another basic block,
  // we must insert an unconditional branch to the fallthrough destination
  // if we are to insert basic blocks at the prior fallthrough point.
  if (FallThrough != nullptr) {
    BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(FallThrough);
  }

  MachineBasicBlock *trueMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *falseMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *continMBB = MF->CreateMachineBasicBlock(LLVM_BB);

  MachineFunction::iterator I;
  for (I = MF->begin(); I != MF->end() && &(*I) != MBB; ++I)
    ;
  if (I != MF->end())
    ++I;
  MF->insert(I, trueMBB);
  MF->insert(I, falseMBB);
  MF->insert(I, continMBB);

  // Transfer remaining instructions and all successors of the current
  // block to the block which will contain the Phi node for the
  // select.
  continMBB->splice(continMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());

  continMBB->transferSuccessorsAndUpdatePHIs(MBB);

  unsigned operandOne = MI.getOperand(1).getReg();
  unsigned operandTwo = MI.getOperand(2).getReg();

  unsigned tempRegOne = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));
  unsigned tempRegTwo = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));

  unsigned destReg = MI.getOperand(0).getReg();

  if (Opc == I8085::SET_ULT_32) {
    BuildMI(MBB, dl, TII.get(I8085::JMP_32_IF_ULT))
        .addReg(operandOne)
        .addReg(operandTwo)
        .addMBB(trueMBB);
    BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(falseMBB);
    MBB->addSuccessor(trueMBB);
    MBB->addSuccessor(falseMBB);
  }

  else if (Opc == I8085::SET_UGT_32) {
    BuildMI(MBB, dl, TII.get(I8085::JMP_32_IF_ULT))
        .addReg(operandTwo)
        .addReg(operandOne)
        .addMBB(trueMBB);
    BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(falseMBB);
    MBB->addSuccessor(trueMBB);
    MBB->addSuccessor(falseMBB);
  }

  else if (Opc == I8085::SET_UGE_32) {
    BuildMI(MBB, dl, TII.get(I8085::JMP_32_IF_ULT))
        .addReg(operandOne)
        .addReg(operandTwo)
        .addMBB(falseMBB);
    BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(trueMBB);
    MBB->addSuccessor(falseMBB);
    MBB->addSuccessor(trueMBB);
  }

  else if (Opc == I8085::SET_ULE_32) {
    BuildMI(MBB, dl, TII.get(I8085::JMP_32_IF_ULT))
        .addReg(operandTwo)
        .addReg(operandOne)
        .addMBB(falseMBB);
    BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(trueMBB);
    MBB->addSuccessor(falseMBB);
    MBB->addSuccessor(trueMBB);
  }

  // Unconditionally flow back to the true block
  BuildMI(falseMBB, dl, TII.get(I8085::MVI))
        .addReg(tempRegTwo, RegState::Define)
        .addImm(0);

  BuildMI(falseMBB, dl, TII.get(I8085::JMP))
            .addMBB(continMBB);


  BuildMI(trueMBB, dl, TII.get(I8085::MVI))
        .addReg(tempRegOne, RegState::Define)
        .addImm(1);

  BuildMI(trueMBB, dl, TII.get(I8085::JMP))
            .addMBB(continMBB);          

  falseMBB->addSuccessor(continMBB);
  trueMBB->addSuccessor(continMBB);
  
  BuildMI(*continMBB, continMBB->begin(), dl, TII.get(I8085::PHI),destReg)
      .addReg(tempRegOne)
      .addMBB(trueMBB)
      .addReg(tempRegTwo)
      .addMBB(falseMBB);

  MI.eraseFromParent();
  return continMBB;
}

MachineBasicBlock *I8085TargetLowering::insertSignedCond32Set(MachineInstr &MI,
                                                  MachineBasicBlock *MBB) const {

  int Opc = MI.getOpcode();
  const I8085InstrInfo &TII = (const I8085InstrInfo &)*MI.getParent()
                                ->getParent()
                                ->getSubtarget()
                                .getInstrInfo();

  DebugLoc dl = MI.getDebugLoc();

  // To "insert" a SELECT instruction, we insert the diamond
  // control-flow pattern. The incoming instruction knows the
  // destination vreg to set, the condition code register to branch
  // on, the true/false values to select between, and a branch opcode
  // to use.

  MachineFunction *MF = MBB->getParent();
  
  const BasicBlock *LLVM_BB = MBB->getBasicBlock();
  MachineBasicBlock *FallThrough = MBB->getFallThrough();

  // If the current basic block falls through to another basic block,
  // we must insert an unconditional branch to the fallthrough destination
  // if we are to insert basic blocks at the prior fallthrough point.
  if (FallThrough != nullptr) {
    BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(FallThrough);
  }

  MachineBasicBlock *continMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *samesignMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *diffsignMBB = MF->CreateMachineBasicBlock(LLVM_BB);

  MachineFunction::iterator I;
  for (I = MF->begin(); I != MF->end() && &(*I) != MBB; ++I)
    ;
  if (I != MF->end())
    ++I;
  MF->insert(I, continMBB);
  MF->insert(I, samesignMBB);
  MF->insert(I, diffsignMBB);

  // Transfer remaining instructions and all successors of the current
  // block to the block which will contain the Phi node for the
  // select.
  continMBB->splice(continMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());

  continMBB->transferSuccessorsAndUpdatePHIs(MBB);

  unsigned operandOne = MI.getOperand(1).getReg();
  unsigned operandTwo = MI.getOperand(2).getReg();
  
  
  unsigned tempRegOne = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));
  unsigned tempRegTwo = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));

  unsigned destReg = MI.getOperand(0).getReg();

  BuildMI(MBB, dl, TII.get(I8085::JMP_32_IF_SAME_SIGN))
        .addReg(operandOne)
        .addReg(operandTwo)
        .addMBB(samesignMBB);

  BuildMI(MBB, dl, TII.get(I8085::JMP))
        .addMBB(diffsignMBB);      


  if(Opc == I8085::SET_GT_32){
    BuildMI(samesignMBB, dl, TII.get(I8085::SET_UGT_32)).addReg(tempRegOne,RegState::Define).addReg(operandOne).addReg(operandTwo);
    BuildMI(diffsignMBB, dl, TII.get(I8085::SET_DIFF_SIGN_GT_32)).addReg(tempRegTwo,RegState::Define).addReg(operandOne).addReg(operandTwo); 
  }

  else if(Opc == I8085::SET_LT_32){
    BuildMI(samesignMBB, dl, TII.get(I8085::SET_ULT_32)).addReg(tempRegOne,RegState::Define).addReg(operandOne).addReg(operandTwo);
    BuildMI(diffsignMBB, dl, TII.get(I8085::SET_DIFF_SIGN_LT_32)).addReg(tempRegTwo,RegState::Define).addReg(operandOne).addReg(operandTwo);  
  }

  else if(Opc == I8085::SET_GE_32){
    BuildMI(samesignMBB, dl, TII.get(I8085::SET_UGE_32)).addReg(tempRegOne,RegState::Define).addReg(operandOne).addReg(operandTwo);
    BuildMI(diffsignMBB, dl, TII.get(I8085::SET_DIFF_SIGN_LT_32)).addReg(tempRegTwo,RegState::Define).addReg(operandTwo).addReg(operandOne);  
  }

  else if(Opc == I8085::SET_LE_32){
    BuildMI(samesignMBB, dl, TII.get(I8085::SET_ULE_32)).addReg(tempRegOne,RegState::Define).addReg(operandOne).addReg(operandTwo); 
    BuildMI(diffsignMBB, dl, TII.get(I8085::SET_DIFF_SIGN_GT_32)).addReg(tempRegTwo,RegState::Define).addReg(operandTwo).addReg(operandOne);
  }
  
  BuildMI(samesignMBB, dl, TII.get(I8085::JMP)) .addMBB(continMBB);
  BuildMI(diffsignMBB, dl, TII.get(I8085::JMP)) .addMBB(continMBB); 
  
  MBB->addSuccessor(samesignMBB);
  MBB->addSuccessor(diffsignMBB);

  samesignMBB->addSuccessor(continMBB);
  diffsignMBB->addSuccessor(continMBB);

  
  BuildMI(*continMBB, continMBB->begin(), dl, TII.get(I8085::PHI),destReg)
      .addReg(tempRegOne)
      .addMBB(samesignMBB)
      .addReg(tempRegTwo)
      .addMBB(diffsignMBB);

  MI.eraseFromParent();
  return continMBB;
}


MachineBasicBlock *I8085TargetLowering::insertDifferentSignedCond32Set(MachineInstr &MI,
                                                  MachineBasicBlock *MBB) const {

  int Opc = MI.getOpcode();
  const I8085InstrInfo &TII = (const I8085InstrInfo &)*MI.getParent()
                                ->getParent()
                                ->getSubtarget()
                                .getInstrInfo();

  DebugLoc dl = MI.getDebugLoc();

  // To "insert" a SELECT instruction, we insert the diamond
  // control-flow pattern. The incoming instruction knows the
  // destination vreg to set, the condition code register to branch
  // on, the true/false values to select between, and a branch opcode
  // to use.

  MachineFunction *MF = MBB->getParent();
  
  const BasicBlock *LLVM_BB = MBB->getBasicBlock();
  MachineBasicBlock *FallThrough = MBB->getFallThrough();

  // If the current basic block falls through to another basic block,
  // we must insert an unconditional branch to the fallthrough destination
  // if we are to insert basic blocks at the prior fallthrough point.
  if (FallThrough != nullptr) {
    BuildMI(MBB, dl, TII.get(I8085::JMP)).addMBB(FallThrough);
  }

  MachineBasicBlock *continMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *firstOperandPos = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *firstOperandNeg = MF->CreateMachineBasicBlock(LLVM_BB);

  MachineFunction::iterator I;
  for (I = MF->begin(); I != MF->end() && &(*I) != MBB; ++I)
    ;
  if (I != MF->end())
    ++I;
  MF->insert(I, continMBB);
  MF->insert(I, firstOperandPos);
  MF->insert(I, firstOperandNeg);

  // Transfer remaining instructions and all successors of the current
  // block to the block which will contain the Phi node for the
  // select.
  continMBB->splice(continMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());

  continMBB->transferSuccessorsAndUpdatePHIs(MBB);

  unsigned operandOne = MI.getOperand(1).getReg(); 
  
  unsigned tempRegOne = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));
  unsigned tempRegTwo = MF->getRegInfo().createVirtualRegister(getRegClassFor(MVT::i8));

  unsigned destReg = MI.getOperand(0).getReg();

  BuildMI(MBB, dl, TII.get(I8085::JMP_32_IF_POSITIVE))
        .addReg(operandOne)
        .addMBB(firstOperandPos);

  BuildMI(MBB, dl, TII.get(I8085::JMP))
        .addMBB(firstOperandNeg);      


  if(Opc == I8085::SET_DIFF_SIGN_LT_32){
    BuildMI(firstOperandPos, dl, TII.get(I8085::MVI)).addReg(tempRegOne,RegState::Define).addImm(0);
    BuildMI(firstOperandNeg, dl, TII.get(I8085::MVI)).addReg(tempRegTwo,RegState::Define).addImm(1);
  }

  else if(Opc == I8085::SET_DIFF_SIGN_GT_32){
    BuildMI(firstOperandPos, dl, TII.get(I8085::MVI)).addReg(tempRegOne,RegState::Define).addImm(1);
    BuildMI(firstOperandNeg, dl, TII.get(I8085::MVI)).addReg(tempRegTwo,RegState::Define).addImm(0); 
  }
  
  BuildMI(firstOperandPos, dl, TII.get(I8085::JMP)) .addMBB(continMBB);
  BuildMI(firstOperandNeg, dl, TII.get(I8085::JMP)) .addMBB(continMBB); 
  
  MBB->addSuccessor(firstOperandPos);
  MBB->addSuccessor(firstOperandNeg);

  firstOperandPos->addSuccessor(continMBB);
  firstOperandNeg->addSuccessor(continMBB);

  
  BuildMI(*continMBB, continMBB->begin(), dl, TII.get(I8085::PHI),destReg)
      .addReg(tempRegOne)
      .addMBB(firstOperandPos)
      .addReg(tempRegTwo)
      .addMBB(firstOperandNeg);

  MI.eraseFromParent();
  return continMBB;
}

} 
