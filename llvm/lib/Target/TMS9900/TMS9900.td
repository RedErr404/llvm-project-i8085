//===-- TMS9900.td - Describe the TMS9900 Target Machine ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the top level entry point for the TMS9900 target.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Target-independent interfaces which we are implementing
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// TMS9900 Subtarget features
//===----------------------------------------------------------------------===//

// Reserve R12 for CRU base address.  Programs that use CRU instructions
// (LDCR/STCR/SBO/SBZ/TB) should compile with -mattr=+reserve-cru so that
// the register allocator will not clobber R12.  When this feature is OFF
// (the default), R12 is available for general allocation.
def FeatureReserveCRU : SubtargetFeature<"reserve-cru",
  "ReserveCRU", "true",
  "Reserve R12 for CRU base address">;

//===----------------------------------------------------------------------===//
// Scheduling Model
//===----------------------------------------------------------------------===//

include "TMS9900Schedule.td"

//===----------------------------------------------------------------------===//
// TMS9900 supported processors
//===----------------------------------------------------------------------===//

def : ProcessorModel<"tms9900", TMS9900Model, []>;

//===----------------------------------------------------------------------===//
// Register File Description
//===----------------------------------------------------------------------===//

include "TMS9900RegisterInfo.td"

//===----------------------------------------------------------------------===//
// Calling Convention Description
//===----------------------------------------------------------------------===//

include "TMS9900CallingConv.td"

//===----------------------------------------------------------------------===//
// Instruction Descriptions
//===----------------------------------------------------------------------===//

include "TMS9900InstrInfo.td"

//===----------------------------------------------------------------------===//
// Target Declaration
//===----------------------------------------------------------------------===//

def TMS9900InstrInfo : InstrInfo;

def TMS9900AsmParser : AsmParser {
  let ShouldEmitMatchRegisterAltName = 1;
}

def TMS9900AsmWriter : AsmWriter {
  string AsmWriterClassName = "InstPrinter";
  bit isMCAsmWriter = 1;
}

def TMS9900 : Target {
  let InstructionSet = TMS9900InstrInfo;
  let AssemblyParsers = [TMS9900AsmParser];
  let AssemblyWriters = [TMS9900AsmWriter];
}
