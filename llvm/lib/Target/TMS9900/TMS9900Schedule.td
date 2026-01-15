//===-- TMS9900Schedule.td - TMS9900 Scheduling Definitions -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains TMS9900 scheduling definitions based on the TMS 9900
// Microprocessor Data Manual (May 1976), Table 3: Instruction Execution Times.
//
// Clock cycle counts are for register-to-register operations. Memory addressing
// modes add additional cycles as specified in Tables A and B of the manual.
//
// The TMS9900 is a single-issue, non-pipelined CPU, so all instructions
// complete before the next begins. The scheduler can still optimize by
// preferring shorter instructions when order doesn't matter.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Scheduling Resources
//===----------------------------------------------------------------------===//

// TMS9900 has a single execution unit - no parallel execution possible
def TMS9900Unit : ProcResource<1> { let BufferSize = 0; }

//===----------------------------------------------------------------------===//
// Scheduling Model
//===----------------------------------------------------------------------===//

def TMS9900Model : SchedMachineModel {
  // LoadLatency: cycles for a load to produce a value
  // MOV *Rx,Rd = 14 base + 4 (indirect) = 18 cycles
  let LoadLatency = 18;

  // HighLatency: threshold for "high latency" instructions
  // DIV can take up to 124 cycles
  let HighLatency = 100;

  // MispredictPenalty: TMS9900 has no branch prediction
  let MispredictPenalty = 0;

  // TMS9900 is single-issue
  let IssueWidth = 1;

  // No micro-op buffering - each instruction runs to completion
  let MicroOpBufferSize = 0;
  let LoopMicroOpBufferSize = 0;

  // We don't have a complete model for every possible instruction variant
  // (especially with all addressing mode combinations)
  let CompleteModel = 0;

  // Enable post-RA scheduling to optimize instruction order
  let PostRAScheduler = 1;

  // No itineraries - use SchedWrite/SchedRead model
  let Itineraries = NoItineraries;
}

//===----------------------------------------------------------------------===//
// SchedWrite Classes - Define write latencies
//===----------------------------------------------------------------------===//

def WriteFast8   : SchedWrite;
def WriteFast    : SchedWrite;
def WriteImm12   : SchedWrite;
def WriteALU     : SchedWrite;
def WriteMem     : SchedWrite;
def WriteSlow    : SchedWrite;
def WriteCRU     : SchedWrite;
def WriteDiv     : SchedWrite;
def WriteControl : SchedWrite;
def WriteControl16 : SchedWrite;
def WriteControl26 : SchedWrite;
def WriteControl36 : SchedWrite;
def WriteBranch  : SchedWrite;
def WriteCall    : SchedWrite;

//===----------------------------------------------------------------------===//
// Write Resources - Map SchedWrites to processor resources and latencies
//===----------------------------------------------------------------------===//

let SchedModel = TMS9900Model in {
  // Fast instructions: 8 cycles
  // STST, STWP, X (plus executed instruction)
  def : WriteRes<WriteFast8, [TMS9900Unit]> { let Latency = 8; }

  // Fast instructions: 10 cycles
  // CLR, DEC, DECT, INC, INCT, INV, SETO, SWPB, LWPI
  def : WriteRes<WriteFast, [TMS9900Unit]> { let Latency = 10; }

  // Immediate/control-lite instructions: 12 cycles
  // LI, NEG, SBO, SBZ, TB, IDLE, CKOF, CKON, RSET, LREX
  def : WriteRes<WriteImm12, [TMS9900Unit]> { let Latency = 12; }

  // Standard ALU: 12-14 cycles
  // MOV (reg-reg), A, S, C, SOC, SZC, XOR, AI, ANDI, CI, LI, ORI, NEG, ABS, COC, CZC
  def : WriteRes<WriteALU, [TMS9900Unit]> { let Latency = 14; }

  // Memory operations: 14 base + addressing mode overhead
  // Use register-indirect (+4) as a baseline, zero wait states assumed.
  def : WriteRes<WriteMem, [TMS9900Unit]> { let Latency = 18; }

  // Control instructions: 16-36 cycles
  // LIMI (16), BLWP (26), XOP (36)
  def : WriteRes<WriteControl, [TMS9900Unit]> { let Latency = 20; }
  def : WriteRes<WriteControl16, [TMS9900Unit]> { let Latency = 16; }
  def : WriteRes<WriteControl26, [TMS9900Unit]> { let Latency = 26; }
  def : WriteRes<WriteControl36, [TMS9900Unit]> { let Latency = 36; }

  // Slow instructions
  // MPY (52), shifts (12+2*count), LDCR (20+2*count)
  def : WriteRes<WriteSlow, [TMS9900Unit]> { let Latency = 52; }

  // CRU store (STCR) can take up to 60 cycles
  def : WriteRes<WriteCRU, [TMS9900Unit]> { let Latency = 60; }

  // DIV (92-124), use a midpoint approximation
  def : WriteRes<WriteDiv, [TMS9900Unit]> { let Latency = 100; }

  // Branch: 8 (not taken) or 10 (taken)
  def : WriteRes<WriteBranch, [TMS9900Unit]> { let Latency = 10; }

  // Call (BL): 12 cycles
  def : WriteRes<WriteCall, [TMS9900Unit]> { let Latency = 12; }

  // Default read advance - no bypass, so 0
  def : ReadAdvance<ReadDefault, 0>;
}

//===----------------------------------------------------------------------===//
// Instruction to Itinerary/SchedWrite Mappings
//
// These are included via InstRW in the instruction definitions.
// See TMS9900InstrInfo.td for the actual instruction definitions.
//===----------------------------------------------------------------------===//

// Note: To use this scheduling model, instructions should be tagged with
// their appropriate SchedRW list. For example:
//
//   def MOVrr : ... {
//     let SchedRW = [WriteALU];
//   }
//
// Or use InstRW to map entire instruction classes:
//
//   let SchedModel = TMS9900Model in {
//     def : InstRW<[WriteALU], (instrs MOVrr, Arr, Srr)>;
//     def : InstRW<[WriteFast], (instrs INCr, DECr, INVr)>;
//     def : InstRW<[WriteSlow], (instrs MPYrr, DIVrr)>;
//   }

//===----------------------------------------------------------------------===//
// Reference: TMS9900 Instruction Timing (from Data Manual Table 3)
//
// Instruction    Cycles  MemAcc  Notes
// ------------------------------------------
// A, S, MOV        14      4     Word arithmetic/move
// AB, SB, MOVB     14      4     Byte arithmetic/move
// ABS (MSB=0)      12      2
// ABS (MSB=1)      14      3
// AI, ANDI, ORI    14      4     Immediate operations
// B                 8      2     Branch
// BL               12      3     Branch and link
// BLWP             26      6     Branch and load WP
// C, CB            14      3     Compare
// CI               14      3     Compare immediate
// CLR              10      3     Clear
// COC, CZC         14      3     Compare ones/zeros
// DEC, DECT        10      3     Decrement
// DIV (overflow)   16      3
// DIV (normal)  92-124     6     Data dependent
// INC, INCT        10      3     Increment
// INV              10      3     Invert
// Jump (taken)     10      1
// Jump (not taken)  8      1
// LDCR           20+2C     3     CRU load
// LI               12      3     Load immediate
// LIMI             16      2     Load interrupt mask
// LWPI             10      2     Load workspace pointer
// MPY              52      5     Multiply
// NEG              12      3     Negate
// RTWP             14      4     Return with WP
// SETO             10      3     Set to ones
// Shift          12+2C     3     SLA, SRA, SRC, SRL
// SOC, SOCB        14      4     Set ones corresponding
// STCR           42-60     4     CRU store (count dependent)
// STST, STWP        8      2     Store status/WP
// SWPB             10      3     Swap bytes
// SZC, SZCB        14      4     Set zeros corresponding
// X                 8      2     Execute
// XOP              36      8     Extended operation
// XOR              14      4     Exclusive or
//
// Address Mode Overhead (Table A - Word operations):
//   Register direct:     +0 cycles, +0 mem
//   Register indirect:   +4 cycles, +1 mem
//   Auto-increment:      +8 cycles, +2 mem
//   Symbolic (@label):   +8 cycles, +1 mem
//   Indexed (@X(R)):     +8 cycles, +2 mem
//
// Address Mode Overhead (Table B - Byte operations):
//   Register direct:     +0 cycles, +0 mem
//   Register indirect:   +4 cycles, +1 mem
//   Auto-increment:      +6 cycles, +2 mem
//   Symbolic (@label):   +8 cycles, +1 mem
//   Indexed (@X(R)):     +8 cycles, +2 mem
//===----------------------------------------------------------------------===//
