//===-- TMS9900CallingConv.td - Calling Conventions --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// TMS9900 Calling Convention
//
// This defines a simple stack-based calling convention:
//
// Arguments:
//   - First 4 arguments in R0-R3
//   - Additional arguments on stack (pushed right-to-left)
//
// Return Values:
//   - Single 16-bit value in R0
//   - 32-bit value in R0 (high) and R1 (low)
//
// Stack:
//   - R10 is the Stack Pointer (SP)
//   - Stack grows DOWNWARD (high to low addresses)
//   - Push: DECT R10, MOV Rx,*R10
//   - Pop:  MOV *R10+,Rx
//
// Preserved Registers (callee-saved):
//   - R10 (SP), R11 (LR), R13, R14, R15
//
// Scratch Registers (caller-saved):
//   - R0-R9, R12
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Return Value Calling Convention
//===----------------------------------------------------------------------===//

def RetCC_TMS9900 : CallingConv<[
  // i8 is promoted to i16
  CCIfType<[i8], CCPromoteToType<i16>>,

  // i16 are returned in R0, R1, R2, R3 (for split i32/i64 values)
  CCIfType<[i16], CCAssignToReg<[R0, R1, R2, R3]>>
]>;

//===----------------------------------------------------------------------===//
// Argument Calling Convention
//===----------------------------------------------------------------------===//

def CC_TMS9900 : CallingConv<[
  // Promote i8 to i16
  CCIfType<[i8], CCPromoteToType<i16>>,

  // First 4 i16 arguments go in R0-R3
  // For i32, LLVM will split into two i16 values, using two registers each
  CCIfType<[i16], CCAssignToReg<[R0, R1, R2, R3]>>,

  // Remaining i16 arguments go on stack, 2-byte aligned
  CCIfType<[i16], CCAssignToStack<2, 2>>
]>;

// Stack-only calling convention for variadic functions.
// All arguments go on the stack so va_arg can walk them sequentially.
def CC_TMS9900_VarArg : CallingConv<[
  // Promote i8 to i16
  CCIfType<[i8], CCPromoteToType<i16>>,

  // All arguments go on stack, 2-byte aligned
  CCIfType<[i16], CCAssignToStack<2, 2>>
]>;

//===----------------------------------------------------------------------===//
// Callee-saved Registers
//===----------------------------------------------------------------------===//

// R11 is the link register - must be saved if function makes calls
def CSR_TMS9900 : CalleeSavedRegs<(add R11, R13, R14, R15)>;
