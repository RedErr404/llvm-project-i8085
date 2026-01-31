//===-- TMS9900InstrFormats.td - TMS9900 Instruction Formats -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// TMS9900 Instruction Format Definitions
//
// Instruction formats based on the TMS9900 Data Manual:
//
// Format 1: Dual operand (both general addressing)
//   [opcode:4][B:1][Td:2][D:4][Ts:2][S:4]
//   Examples: A, AB, C, CB, S, SB, SOC, SZC, MOV, MOVB
//
// Format 2: Dual operand (source general, dest register)
//   [opcode:6][D:4][Ts:2][S:4]
//   Examples: COC, CZC, XOR, MPY, DIV
//
// Format 3: Single operand
//   [opcode:10][Ts:2][S:4]
//   Examples: B, BL, BLWP, CLR, SETO, INV, NEG, INC, DEC
//
// Format 4: CRU multi-bit
//   [opcode:6][C:4][Ts:2][S:4]
//   Examples: LDCR, STCR
//
// Format 5: CRU single-bit
//   [opcode:8][disp:8]
//   Examples: SBO, SBZ, TB
//
// Format 6: Jump (PC-relative)
//   [opcode:8][disp:8]
//   Examples: JEQ, JNE, JMP, etc.
//
// Format 7: Shift
//   [opcode:8][C:4][W:4]
//   Examples: SLA, SRA, SRC, SRL
//
// Format 8: Immediate register
//   [opcode:8][N:4][W:4] + [imm:16]
//   Examples: AI, ANDI, CI, LI, ORI
//
// Format 9: Internal register immediate
//   [opcode:11][N:5] + [imm:16]
//   Examples: LWPI, LIMI
//
// Format 10: Internal register store
//   [opcode:11][N:1][W:4]
//   Examples: STST, STWP
//
// Format 11: RTWP
//   [opcode:11][N:5]
//
// Format 12: External
//   [opcode:11][N:5]
//   Examples: IDLE, RSET, CKOF, CKON, LREX
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Addressing Mode Encodings
//===----------------------------------------------------------------------===//

// Ts/Td field encoding:
//   00 = Register direct (Rx)
//   01 = Register indirect (*Rx)
//   10 = Symbolic @LABEL (when S/D=0) or Indexed @LABEL(Rx) (when S/D!=0)
//   11 = Register indirect auto-increment (*Rx+)

//===----------------------------------------------------------------------===//
// Base Instruction Class
//===----------------------------------------------------------------------===//

class TMS9900Inst<dag outs, dag ins, string asmstr, list<dag> pattern>
    : Instruction {
  let Namespace = "TMS9900";

  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;
  let SchedRW = [WriteALU];

  // Size in bytes (most instructions are 2 bytes, some have additional words)
  bits<8> Size = 2;

  let TSFlags{7-0} = Size;
}

//===----------------------------------------------------------------------===//
// Pseudo Instructions
//===----------------------------------------------------------------------===//

class Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
  let SchedRW = [];
}

//===----------------------------------------------------------------------===//
// Format 1: Dual Operand with General Addressing (both operands)
// [opcode:4][B:1][Td:2][D:4][Ts:2][S:4]
//===----------------------------------------------------------------------===//

// TMS9900 Instruction Encoding Notes:
//
// The manual describes Format 1 as [op:4][B:1][Td:2][D:4][Ts:2][S:4] = 17 bits.
// However, B is actually encoded as bit 0 of the opcode (MOV=0xC, MOVB=0xD).
//
// Correct Format 1 layout (16 bits):
//   [opcode:4][dest:6][src:6]
// Where each 6-bit operand = [T:2][reg:4]
//   T=00: register direct
//   T=01: register indirect
//   T=10: symbolic/indexed
//   T=11: register indirect auto-increment
//
// Bit layout (LLVM-style, bit 0 = LSB):
//   Bits 15-12: opcode (including byte flag as bit 12)
//   Bits 11-6: destination operand (Td:2 + D:4)
//   Bits 5-0: source operand (Ts:2 + S:4)
//
// Example: MOV R1,R2 = 0xC082
//   0xC082 = 1100 0000 1000 0010
//   opcode = 1100 (MOV)
//   dest = 000010 = T=00 (reg direct), reg=0010 (R2)
//   src = 000001 = T=00 (reg direct), reg=0001 (R1)

// Simplified format for register-to-register operations (Ts=Td=00)
// Used for MOV and other non-modifying register transfers
class Format1_Reg<bits<4> opcode, bit byteflag, dag outs, dag ins,
                  string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  bits<4> rd;   // Destination register
  bits<4> rs;   // Source register

  bits<16> Inst;

  // Combine opcode with byte flag
  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  // Destination operand (Td=00 for register direct)
  let Inst{11-10} = 0b00;     // Td = register direct
  let Inst{9-6} = rd;         // D = destination register
  // Source operand (Ts=00 for register direct)
  let Inst{5-4} = 0b00;       // Ts = register direct
  let Inst{3-0} = rs;         // S = source register
}

// Format1_Reg variant for two-address operations (A, S, SOC, SZC)
// Operands: (outs $rd), (ins $rs1, $rs2) with $rd = $rs1 constraint
// Encodes $rd and $rs2 (the explicit source operand)
class Format1_Reg_TwoAddr<bits<4> opcode, bit byteflag, dag outs, dag ins,
                          string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  bits<4> rd;   // Destination register (output, tied to rs1)
  bits<4> rs2;  // Source register (explicit source operand)

  bits<16> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  let Inst{11-10} = 0b00;     // Td = register direct
  let Inst{9-6} = rd;         // D = destination register
  let Inst{5-4} = 0b00;       // Ts = register direct
  let Inst{3-0} = rs2;        // S = source register
}

// Format1_Reg variant for compare operations (C)
// Operands: (outs), (ins $rs1, $rs2) - no output, just sets flags
// Encodes both rs1 (as destination field) and rs2 (as source field)
class Format1_Reg_Cmp<bits<4> opcode, bit byteflag, dag outs, dag ins,
                      string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  bits<4> rs1;  // First operand (in destination field position)
  bits<4> rs2;  // Second operand (source)

  bits<16> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  let Inst{11-10} = 0b00;     // Td = register direct
  let Inst{9-6} = rs1;        // D = first operand
  let Inst{5-4} = 0b00;       // Ts = register direct
  let Inst{3-0} = rs2;        // S = second operand
}

//===----------------------------------------------------------------------===//
// Format 2: Dual Operand (Source General, Dest Register)
// [opcode:6][D:4][Ts:2][S:4]
//===----------------------------------------------------------------------===//

class Format2<bits<6> opcode, dag outs, dag ins, string asmstr,
              list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  bits<4> rd;   // Destination register
  bits<6> src;  // Ts:2 + S:4

  bits<16> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = rd;
  let Inst{5-4} = src{5-4};   // Ts
  let Inst{3-0} = src{3-0};   // S
}

class Format2_Reg<bits<6> opcode, dag outs, dag ins, string asmstr,
                  list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  bits<4> rd;   // Destination register
  bits<4> rs;   // Source register

  bits<16> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = rd;
  let Inst{5-4} = 0b00;       // Ts = register direct
  let Inst{3-0} = rs;
}

class Format2_Ind<bits<6> opcode, dag outs, dag ins, string asmstr,
                  list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> rd;   // Destination register
  bits<4> rs;   // Source register (contains address)

  bits<16> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = rd;
  let Inst{5-4} = 0b01;       // Ts = register indirect
  let Inst{3-0} = rs;
}

class Format2_AutoInc<bits<6> opcode, dag outs, dag ins, string asmstr,
                      list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> rd;   // Destination register
  bits<4> rs;   // Source register (contains address)

  bits<16> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = rd;
  let Inst{5-4} = 0b11;       // Ts = auto-increment
  let Inst{3-0} = rs;
}

class Format2_Sym<bits<6> opcode, dag outs, dag ins, string asmstr,
                  list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> rd;    // Destination register
  bits<16> addr; // Symbolic address

  bits<32> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = rd;
  let Inst{5-4} = 0b10;       // Ts = symbolic/indexed
  let Inst{3-0} = 0b0000;     // S = 0 (symbolic)
  let Inst{31-16} = addr;

  let Size = 4;
}

class Format2_Idx<bits<6> opcode, dag outs, dag ins, string asmstr,
                  list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> rd;     // Destination register
  bits<4> ri;     // Index register
  bits<16> offset;// Offset

  bits<32> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = rd;
  let Inst{5-4} = 0b10;       // Ts = symbolic/indexed
  let Inst{3-0} = ri;         // S = index register
  let Inst{31-16} = offset;

  let Size = 4;
}

class Format2_Ind_R0<bits<6> opcode, dag outs, dag ins, string asmstr,
                     list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> rs;   // Source register (contains address)

  bits<16> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = 0b0000;     // rd = R0 (hardcoded)
  let Inst{5-4} = 0b01;       // Ts = register indirect
  let Inst{3-0} = rs;
}

class Format2_AutoInc_R0<bits<6> opcode, dag outs, dag ins, string asmstr,
                         list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> rs;   // Source register (contains address)

  bits<16> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = 0b0000;     // rd = R0 (hardcoded)
  let Inst{5-4} = 0b11;       // Ts = auto-increment
  let Inst{3-0} = rs;
}

class Format2_Sym_R0<bits<6> opcode, dag outs, dag ins, string asmstr,
                     list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<16> addr; // Symbolic address

  bits<32> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = 0b0000;     // rd = R0 (hardcoded)
  let Inst{5-4} = 0b10;       // Ts = symbolic/indexed
  let Inst{3-0} = 0b0000;     // S = 0 (symbolic)
  let Inst{31-16} = addr;

  let Size = 4;
}

class Format2_Idx_R0<bits<6> opcode, dag outs, dag ins, string asmstr,
                     list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> ri;      // Index register
  bits<16> offset; // Offset

  bits<32> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = 0b0000;     // rd = R0 (hardcoded)
  let Inst{5-4} = 0b10;       // Ts = symbolic/indexed
  let Inst{3-0} = ri;         // S = index register
  let Inst{31-16} = offset;

  let Size = 4;
}

// Format2_Reg variant for MPY/DIV with implicit R0 destination
// These instructions use R0:R1 as destination (hardcoded)
class Format2_Reg_R0<bits<6> opcode, dag outs, dag ins, string asmstr,
                     list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  bits<4> rs;   // Source register

  bits<16> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = 0b0000;     // rd = R0 (hardcoded)
  let Inst{5-4} = 0b00;       // Ts = register direct
  let Inst{3-0} = rs;
}

// Format2_Reg variant for two-address operations (XOR)
// Operands: (outs $rd), (ins $rs1, $rs2) with $rd = $rs1 constraint
// Encodes $rd and $rs2 (the explicit source operand)
class Format2_Reg_TwoAddr<bits<6> opcode, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  bits<4> rd;   // Destination register (output, tied to rs1)
  bits<4> rs2;  // Source register (explicit source operand)

  bits<16> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = rd;
  let Inst{5-4} = 0b00;       // Ts = register direct
  let Inst{3-0} = rs2;
}

//===----------------------------------------------------------------------===//
// Format 3: Single Operand
// [opcode:10][Ts:2][S:4]
//===----------------------------------------------------------------------===//

class Format3<bits<10> opcode, dag outs, dag ins, string asmstr,
              list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  bits<6> src;  // Ts:2 + S:4

  bits<16> Inst;

  let Inst{15-6} = opcode;
  let Inst{5-4} = src{5-4};   // Ts
  let Inst{3-0} = src{3-0};   // S
}

class Format3_Reg<bits<10> opcode, dag outs, dag ins, string asmstr,
                  list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  bits<4> rs;   // Source/dest register

  bits<16> Inst;

  let Inst{15-6} = opcode;
  let Inst{5-4} = 0b00;       // Ts = register direct
  let Inst{3-0} = rs;
}

// Format3_Reg variant with register indirect addressing (*Rs)
class Format3_IndReg<bits<10> opcode, dag outs, dag ins, string asmstr,
                     list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  bits<4> rs;   // Source/dest register (contains address)

  bits<16> Inst;

  let Inst{15-6} = opcode;
  let Inst{5-4} = 0b01;       // Ts = register indirect
  let Inst{3-0} = rs;
}

// Format3_Reg variant with hardcoded R11 (for RET = B *R11)
class Format3_Reg_R11<bits<10> opcode, dag outs, dag ins, string asmstr,
                      list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  bits<16> Inst;

  let Inst{15-6} = opcode;
  let Inst{5-4} = 0b01;       // Ts = register indirect
  let Inst{3-0} = 0b1011;     // R11 (link register)
}

// Format3 variant with symbolic addressing: @addr
class Format3_Sym<bits<10> opcode, dag outs, dag ins, string asmstr,
                  list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  bits<16> addr;  // Symbolic address

  bits<32> Inst;

  let Inst{15-6} = opcode;
  let Inst{5-4} = 0b10;       // Ts = symbolic/indexed
  let Inst{3-0} = 0b0000;     // S = 0 (symbolic)
  let Inst{31-16} = addr;     // Second word: address

  let Size = 4;
}

// Format3 variant with indexed addressing: @offset(Ri)
class Format3_Idx<bits<10> opcode, dag outs, dag ins, string asmstr,
                  list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  bits<16> offset;  // Offset
  bits<4> ri;       // Index register

  bits<32> Inst;

  let Inst{15-6} = opcode;
  let Inst{5-4} = 0b10;       // Ts = symbolic/indexed
  let Inst{3-0} = ri;         // S = index register
  let Inst{31-16} = offset;   // Second word: offset

  let Size = 4;
}

// Format3 variant with auto-increment: *Rs+
class Format3_AutoInc<bits<10> opcode, dag outs, dag ins, string asmstr,
                      list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  bits<4> rs;
  bits<16> Inst;

  let Inst{15-6} = opcode;
  let Inst{5-4} = 0b11;       // Ts = auto-increment
  let Inst{3-0} = rs;
}

//===----------------------------------------------------------------------===//
// Format 1 with Memory Addressing
// For load: MOV @addr,Rd or MOV *Rs,Rd
// For store: MOV Rs,@addr or MOV Rs,*Rd
//===----------------------------------------------------------------------===//

// MOV with symbolic source addressing: MOV @addr,Rd
// [opcode:4][B:1][Td:2][D:4][Ts:2][S:4] + [addr:16]
// Ts=10, S=0 for symbolic addressing
class Format1_SymLoad<bits<4> opcode, bit byteflag, dag outs, dag ins,
                      string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> rd;     // Destination register
  bits<16> addr;  // Memory address

  bits<32> Inst;

  // First word: opcode + addressing modes
  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  // Destination operand (Td=00 for register direct)
  let Inst{11-10} = 0b00;     // Td = register direct
  let Inst{9-6} = rd;         // D = destination register
  // Source operand (Ts=10, S=0 for symbolic addressing)
  let Inst{5-4} = 0b10;       // Ts = symbolic/indexed
  let Inst{3-0} = 0b0000;     // S = 0 for symbolic
  // Second word: address
  let Inst{31-16} = addr;

  let Size = 4;  // Two words (instruction + address)
}

// MOV with symbolic destination addressing: MOV Rs,@addr
class Format1_SymStore<bits<4> opcode, bit byteflag, dag outs, dag ins,
                       string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<16> addr;  // Memory address
  bits<4> rs;     // Source register

  bits<32> Inst;

  // First word: opcode + addressing modes
  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  // Destination operand (Td=10, D=0 for symbolic addressing)
  let Inst{11-10} = 0b10;     // Td = symbolic
  let Inst{9-6} = 0b0000;     // D = 0 for symbolic
  // Source operand (Ts=00 for register direct)
  let Inst{5-4} = 0b00;       // Ts = register direct
  let Inst{3-0} = rs;         // S = source register
  // Second word: address
  let Inst{31-16} = addr;

  let Size = 4;  // Two words (instruction + address)
}

// MOV with indirect source: MOV *Rs,Rd
class Format1_IndLoad<bits<4> opcode, bit byteflag, dag outs, dag ins,
                      string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> rd;   // Destination register
  bits<4> rs;   // Source register (contains address)

  bits<16> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  // Destination operand (Td=00 for register direct)
  let Inst{11-10} = 0b00;     // Td = register direct
  let Inst{9-6} = rd;         // D = destination register
  // Source operand (Ts=01 for indirect)
  let Inst{5-4} = 0b01;       // Ts = register indirect
  let Inst{3-0} = rs;         // S = source register
}

// MOV with indirect destination: MOV Rs,*Rd
class Format1_IndStore<bits<4> opcode, bit byteflag, dag outs, dag ins,
                       string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> rd;   // Destination register (contains address)
  bits<4> rs;   // Source register

  bits<16> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  // Destination operand (Td=01 for indirect)
  let Inst{11-10} = 0b01;     // Td = register indirect
  let Inst{9-6} = rd;         // D = destination register
  // Source operand (Ts=00 for register direct)
  let Inst{5-4} = 0b00;       // Ts = register direct
  let Inst{3-0} = rs;         // S = source register
}

// MOV with indexed source: MOV @offset(Ri),Rd
class Format1_IdxLoad<bits<4> opcode, bit byteflag, dag outs, dag ins,
                      string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> rd;     // Destination register
  bits<4> ri;     // Index register (1-15, not 0)
  bits<16> offset;// Offset

  bits<32> Inst;

  // First word: opcode + addressing modes
  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  // Destination operand (Td=00 for register direct)
  let Inst{11-10} = 0b00;     // Td = register direct
  let Inst{9-6} = rd;         // D = destination register
  // Source operand (Ts=10 for indexed, S=index register)
  let Inst{5-4} = 0b10;       // Ts = symbolic/indexed
  let Inst{3-0} = ri;         // S = index register
  // Second word: offset
  let Inst{31-16} = offset;

  let Size = 4;  // Two words (instruction + offset)
}

// MOV with indexed destination: MOV Rs,@offset(Ri)
class Format1_IdxStore<bits<4> opcode, bit byteflag, dag outs, dag ins,
                       string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> ri;     // Index register (1-15, not 0)
  bits<16> offset;// Offset
  bits<4> rs;     // Source register

  bits<32> Inst;

  // First word: opcode + addressing modes
  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  // Destination operand (Td=10 for indexed, D=index register)
  let Inst{11-10} = 0b10;     // Td = symbolic/indexed
  let Inst{9-6} = ri;         // D = index register
  // Source operand (Ts=00 for register direct)
  let Inst{5-4} = 0b00;       // Ts = register direct
  let Inst{3-0} = rs;         // S = source register
  // Second word: offset
  let Inst{31-16} = offset;

  let Size = 4;  // Two words (instruction + offset)
}

//===----------------------------------------------------------------------===//
// Format 1 with Auto-Increment Addressing
// MOV *Rs+,Rd - Load with post-increment
// The register Rs is incremented by 2 (word) or 1 (byte) after the access
//===----------------------------------------------------------------------===//

// MOV with auto-increment source: MOV *Rs+,Rd
class Format1_AutoIncLoad<bits<4> opcode, bit byteflag, dag outs, dag ins,
                          string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> rd;   // Destination register
  bits<4> rs;   // Source register (contains address, will be incremented)

  bits<16> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  // Destination operand (Td=00 for register direct)
  let Inst{11-10} = 0b00;     // Td = register direct
  let Inst{9-6} = rd;         // D = destination register
  // Source operand (Ts=11 for auto-increment)
  let Inst{5-4} = 0b11;       // Ts = register indirect auto-increment
  let Inst{3-0} = rs;         // S = source register
}

// MOV with auto-increment destination: MOV Rs,*Rd+
// Note: Auto-increment on destination is unusual but valid
class Format1_AutoIncStore<bits<4> opcode, bit byteflag, dag outs, dag ins,
                           string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> rd;   // Destination register (contains address, will be incremented)
  bits<4> rs;   // Source register

  bits<16> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  // Destination operand (Td=11 for auto-increment)
  let Inst{11-10} = 0b11;     // Td = register indirect auto-increment
  let Inst{9-6} = rd;         // D = destination register
  // Source operand (Ts=00 for register direct)
  let Inst{5-4} = 0b00;       // Ts = register direct
  let Inst{3-0} = rs;         // S = source register
}

//===----------------------------------------------------------------------===//
// Format 1 with Symbolic/Indexed Addressing on Both Operands (Mem-to-Mem)
// These forms use two extra words (source first, destination second).
//===----------------------------------------------------------------------===//

// Symbolic source and symbolic destination: OP @src,@dst
class Format1_SymSym<bits<4> opcode, bit byteflag, dag outs, dag ins,
                     string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<16> src;
  bits<16> dst;

  bits<48> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  let Inst{11-10} = 0b10;     // Td = symbolic/indexed
  let Inst{9-6} = 0b0000;     // D = 0 (symbolic)
  let Inst{5-4} = 0b10;       // Ts = symbolic/indexed
  let Inst{3-0} = 0b0000;     // S = 0 (symbolic)
  let Inst{31-16} = src;      // Source address
  let Inst{47-32} = dst;      // Destination address

  let Size = 6;
}

// Symbolic source, indexed destination: OP @src,@off(Rd)
class Format1_SymIdx<bits<4> opcode, bit byteflag, dag outs, dag ins,
                     string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<16> src;
  bits<4> ri;
  bits<16> dst;

  bits<48> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  let Inst{11-10} = 0b10;     // Td = symbolic/indexed
  let Inst{9-6} = ri;         // D = destination index register
  let Inst{5-4} = 0b10;       // Ts = symbolic/indexed
  let Inst{3-0} = 0b0000;     // S = 0 (symbolic)
  let Inst{31-16} = src;      // Source address
  let Inst{47-32} = dst;      // Destination offset

  let Size = 6;
}

// Indexed source, symbolic destination: OP @off(Rs),@dst
class Format1_IdxSym<bits<4> opcode, bit byteflag, dag outs, dag ins,
                     string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<16> src;
  bits<4> ri;
  bits<16> dst;

  bits<48> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  let Inst{11-10} = 0b10;     // Td = symbolic/indexed
  let Inst{9-6} = 0b0000;     // D = 0 (symbolic)
  let Inst{5-4} = 0b10;       // Ts = symbolic/indexed
  let Inst{3-0} = ri;         // S = source index register
  let Inst{31-16} = src;      // Source offset
  let Inst{47-32} = dst;      // Destination address

  let Size = 6;
}

// Indexed source and indexed destination: OP @off(Rs),@off(Rd)
class Format1_IdxIdx<bits<4> opcode, bit byteflag, dag outs, dag ins,
                     string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<16> src;
  bits<4> src_ri;
  bits<16> dst;
  bits<4> dst_ri;

  bits<48> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  let Inst{11-10} = 0b10;     // Td = symbolic/indexed
  let Inst{9-6} = dst_ri;     // D = destination index register
  let Inst{5-4} = 0b10;       // Ts = symbolic/indexed
  let Inst{3-0} = src_ri;     // S = source index register
  let Inst{31-16} = src;      // Source offset
  let Inst{47-32} = dst;      // Destination offset

  let Size = 6;
}

//===----------------------------------------------------------------------===//
// Format 1 with Remaining Addressing Mode Combinations
// The TMS9900 supports all 16 combinations of Ts (2 bits) x Td (2 bits).
// The classes above cover Reg-to-Reg, Load (any src → Reg dest),
// Store (Reg src → any dest), and Sym/Idx-to-Sym/Idx.
// Below are the remaining combinations involving indirect (*Rn) and
// auto-increment (*Rn+) with non-register partners.
//===----------------------------------------------------------------------===//

// Indirect-to-Indirect: OP *Rs,*Rd
class Format1_IndInd<bits<4> opcode, bit byteflag, dag outs, dag ins,
                     string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> rd;
  bits<4> rs;

  bits<16> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  let Inst{11-10} = 0b01;     // Td = indirect
  let Inst{9-6} = rd;
  let Inst{5-4} = 0b01;       // Ts = indirect
  let Inst{3-0} = rs;
}

// Symbolic-to-Indirect: OP @addr,*Rd
class Format1_SymInd<bits<4> opcode, bit byteflag, dag outs, dag ins,
                     string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> rd;
  bits<16> addr;

  bits<32> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  let Inst{11-10} = 0b01;     // Td = indirect
  let Inst{9-6} = rd;
  let Inst{5-4} = 0b10;       // Ts = symbolic
  let Inst{3-0} = 0b0000;     // S = 0 (symbolic)
  let Inst{31-16} = addr;

  let Size = 4;
}

// Indexed-to-Indirect: OP @off(Rs),*Rd
class Format1_IdxInd<bits<4> opcode, bit byteflag, dag outs, dag ins,
                     string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> rd;
  bits<4> ri;
  bits<16> offset;

  bits<32> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  let Inst{11-10} = 0b01;     // Td = indirect
  let Inst{9-6} = rd;
  let Inst{5-4} = 0b10;       // Ts = indexed
  let Inst{3-0} = ri;
  let Inst{31-16} = offset;

  let Size = 4;
}

// AutoInc-to-Indirect: OP *Rs+,*Rd
class Format1_AutoIncInd<bits<4> opcode, bit byteflag, dag outs, dag ins,
                         string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> rd;
  bits<4> rs;

  bits<16> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  let Inst{11-10} = 0b01;     // Td = indirect
  let Inst{9-6} = rd;
  let Inst{5-4} = 0b11;       // Ts = auto-increment
  let Inst{3-0} = rs;
}

// Indirect-to-Symbolic: OP *Rs,@addr
class Format1_IndSym<bits<4> opcode, bit byteflag, dag outs, dag ins,
                     string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<16> addr;
  bits<4> rs;

  bits<32> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  let Inst{11-10} = 0b10;     // Td = symbolic
  let Inst{9-6} = 0b0000;     // D = 0 (symbolic)
  let Inst{5-4} = 0b01;       // Ts = indirect
  let Inst{3-0} = rs;
  let Inst{31-16} = addr;

  let Size = 4;
}

// Indirect-to-Indexed: OP *Rs,@off(Rd)
class Format1_IndIdx<bits<4> opcode, bit byteflag, dag outs, dag ins,
                     string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> ri;
  bits<16> offset;
  bits<4> rs;

  bits<32> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  let Inst{11-10} = 0b10;     // Td = indexed
  let Inst{9-6} = ri;
  let Inst{5-4} = 0b01;       // Ts = indirect
  let Inst{3-0} = rs;
  let Inst{31-16} = offset;

  let Size = 4;
}

// Indirect-to-AutoInc: OP *Rs,*Rd+
class Format1_IndAutoInc<bits<4> opcode, bit byteflag, dag outs, dag ins,
                         string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> rd;
  bits<4> rs;

  bits<16> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  let Inst{11-10} = 0b11;     // Td = auto-increment
  let Inst{9-6} = rd;
  let Inst{5-4} = 0b01;       // Ts = indirect
  let Inst{3-0} = rs;
}

// Symbolic-to-AutoInc: OP @addr,*Rd+
class Format1_SymAutoInc<bits<4> opcode, bit byteflag, dag outs, dag ins,
                         string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> rd;
  bits<16> addr;

  bits<32> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  let Inst{11-10} = 0b11;     // Td = auto-increment
  let Inst{9-6} = rd;
  let Inst{5-4} = 0b10;       // Ts = symbolic
  let Inst{3-0} = 0b0000;     // S = 0 (symbolic)
  let Inst{31-16} = addr;

  let Size = 4;
}

// Indexed-to-AutoInc: OP @off(Rs),*Rd+
class Format1_IdxAutoInc<bits<4> opcode, bit byteflag, dag outs, dag ins,
                         string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> rd;
  bits<4> ri;
  bits<16> offset;

  bits<32> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  let Inst{11-10} = 0b11;     // Td = auto-increment
  let Inst{9-6} = rd;
  let Inst{5-4} = 0b10;       // Ts = indexed
  let Inst{3-0} = ri;
  let Inst{31-16} = offset;

  let Size = 4;
}

// AutoInc-to-Symbolic: OP *Rs+,@addr
class Format1_AutoIncSym<bits<4> opcode, bit byteflag, dag outs, dag ins,
                         string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<16> addr;
  bits<4> rs;

  bits<32> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  let Inst{11-10} = 0b10;     // Td = symbolic
  let Inst{9-6} = 0b0000;     // D = 0 (symbolic)
  let Inst{5-4} = 0b11;       // Ts = auto-increment
  let Inst{3-0} = rs;
  let Inst{31-16} = addr;

  let Size = 4;
}

// AutoInc-to-Indexed: OP *Rs+,@off(Rd)
class Format1_AutoIncIdx<bits<4> opcode, bit byteflag, dag outs, dag ins,
                         string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> ri;
  bits<16> offset;
  bits<4> rs;

  bits<32> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  let Inst{11-10} = 0b10;     // Td = indexed
  let Inst{9-6} = ri;
  let Inst{5-4} = 0b11;       // Ts = auto-increment
  let Inst{3-0} = rs;
  let Inst{31-16} = offset;

  let Size = 4;
}

// AutoInc-to-AutoInc: OP *Rs+,*Rd+
class Format1_AutoIncAutoInc<bits<4> opcode, bit byteflag, dag outs, dag ins,
                             string asmstr, list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteMem];
  bits<4> rd;
  bits<4> rs;

  bits<16> Inst;

  let Inst{15-13} = opcode{3-1};
  let Inst{12} = byteflag;
  let Inst{11-10} = 0b11;     // Td = auto-increment
  let Inst{9-6} = rd;
  let Inst{5-4} = 0b11;       // Ts = auto-increment
  let Inst{3-0} = rs;
}

//===----------------------------------------------------------------------===//
// Format 6: Jump (PC-relative)
// [opcode:8][disp:8]
//===----------------------------------------------------------------------===//

class Format6<bits<8> opcode, dag outs, dag ins, string asmstr,
              list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteBranch];
  bits<8> disp;   // Signed displacement (word offset)

  bits<16> Inst;

  let Inst{15-8} = opcode;
  let Inst{7-0} = disp;

  let isBranch = 1;
  let isTerminator = 1;
}

//===----------------------------------------------------------------------===//
// Format 4: CRU multi-bit
// [opcode:6][C:4][Ts:2][S:4]
//===----------------------------------------------------------------------===//

class Format4<bits<6> opcode, dag outs, dag ins, string asmstr,
              list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteSlow];
  bits<4> cnt;   // Count field
  bits<6> src;   // Ts:2 + S:4

  bits<16> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = cnt;
  let Inst{5-4} = src{5-4};
  let Inst{3-0} = src{3-0};
}

class Format4_Reg<bits<6> opcode, dag outs, dag ins, string asmstr,
                  list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteSlow];
  bits<4> cnt;
  bits<4> rs;

  bits<16> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = cnt;
  let Inst{5-4} = 0b00;
  let Inst{3-0} = rs;
}

class Format4_Ind<bits<6> opcode, dag outs, dag ins, string asmstr,
                  list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteSlow];
  bits<4> cnt;
  bits<4> rs;

  bits<16> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = cnt;
  let Inst{5-4} = 0b01;
  let Inst{3-0} = rs;
}

class Format4_AutoInc<bits<6> opcode, dag outs, dag ins, string asmstr,
                      list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteSlow];
  bits<4> cnt;
  bits<4> rs;

  bits<16> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = cnt;
  let Inst{5-4} = 0b11;
  let Inst{3-0} = rs;
}

class Format4_Sym<bits<6> opcode, dag outs, dag ins, string asmstr,
                  list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteSlow];
  bits<4> cnt;
  bits<16> addr;

  bits<32> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = cnt;
  let Inst{5-4} = 0b10;
  let Inst{3-0} = 0b0000;
  let Inst{31-16} = addr;

  let Size = 4;
}

class Format4_Idx<bits<6> opcode, dag outs, dag ins, string asmstr,
                  list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteSlow];
  bits<4> cnt;
  bits<16> offset;
  bits<4> ri;

  bits<32> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = cnt;
  let Inst{5-4} = 0b10;
  let Inst{3-0} = ri;
  let Inst{31-16} = offset;

  let Size = 4;
}

//===----------------------------------------------------------------------===//
// XOP format (extended operation)
// [opcode:6][D:4][Ts:2][S:4]
//===----------------------------------------------------------------------===//

class FormatXOP_Reg<bits<6> opcode, dag outs, dag ins, string asmstr,
                    list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteControl36];
  bits<4> vec;
  bits<4> rs;

  bits<16> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = vec;
  let Inst{5-4} = 0b00;
  let Inst{3-0} = rs;
}

class FormatXOP_Ind<bits<6> opcode, dag outs, dag ins, string asmstr,
                    list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteControl36];
  bits<4> vec;
  bits<4> rs;

  bits<16> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = vec;
  let Inst{5-4} = 0b01;
  let Inst{3-0} = rs;
}

class FormatXOP_AutoInc<bits<6> opcode, dag outs, dag ins, string asmstr,
                        list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteControl36];
  bits<4> vec;
  bits<4> rs;

  bits<16> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = vec;
  let Inst{5-4} = 0b11;
  let Inst{3-0} = rs;
}

class FormatXOP_Sym<bits<6> opcode, dag outs, dag ins, string asmstr,
                    list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteControl36];
  bits<4> vec;
  bits<16> addr;

  bits<32> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = vec;
  let Inst{5-4} = 0b10;
  let Inst{3-0} = 0b0000;
  let Inst{31-16} = addr;

  let Size = 4;
}

class FormatXOP_Idx<bits<6> opcode, dag outs, dag ins, string asmstr,
                    list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteControl36];
  bits<4> vec;
  bits<16> offset;
  bits<4> ri;

  bits<32> Inst;

  let Inst{15-10} = opcode;
  let Inst{9-6} = vec;
  let Inst{5-4} = 0b10;
  let Inst{3-0} = ri;
  let Inst{31-16} = offset;

  let Size = 4;
}

//===----------------------------------------------------------------------===//
// Format 5: CRU single-bit
// [opcode:8][disp:8]
//===----------------------------------------------------------------------===//

class Format5<bits<8> opcode, dag outs, dag ins, string asmstr,
              list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteImm12];
  bits<8> disp;
  bits<16> Inst;

  let Inst{15-8} = opcode;
  let Inst{7-0} = disp;
}

//===----------------------------------------------------------------------===//
// Format 7: Shift
// [opcode:8][C:4][W:4]
//===----------------------------------------------------------------------===//

class Format7<bits<8> opcode, dag outs, dag ins, string asmstr,
              list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteSlow];
  bits<4> count;  // Shift count (0 = use R0)
  bits<4> rw;     // Register to shift

  bits<16> Inst;

  let Inst{15-8} = opcode;
  let Inst{7-4} = count;
  let Inst{3-0} = rw;
}

// Format7 variant for two-address shift instructions
// Operands: (outs $rd), (ins $rs, $cnt) with $rd = $rs constraint
// The register is encoded from $rd, count from $cnt
class Format7_TwoAddr<bits<8> opcode, dag outs, dag ins, string asmstr,
                      list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteSlow];
  bits<4> cnt;    // Shift count operand
  bits<4> rd;     // Register to shift (output, tied to input)

  bits<16> Inst;

  let Inst{15-8} = opcode;
  let Inst{7-4} = cnt;
  let Inst{3-0} = rd;
}

// Format7 variant for shifts where count comes from R0 (count field = 0)
// Operands: (outs $rd), (ins $rs) with $rd = $rs constraint
class Format7_R0Count<bits<8> opcode, dag outs, dag ins, string asmstr,
                      list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  let SchedRW = [WriteSlow];
  bits<4> rd;     // Register to shift (output, tied to input)

  bits<16> Inst;

  let Inst{15-8} = opcode;
  let Inst{7-4} = 0b0000;     // count = 0 (use R0)
  let Inst{3-0} = rd;
}

//===----------------------------------------------------------------------===//
// Format 8: Immediate Register
// [opcode:8][N:4][W:4] + [imm:16]
// (Actually opcode is in bits 0-10, but simplified here)
//===----------------------------------------------------------------------===//

class Format8<bits<4> subop, dag outs, dag ins, string asmstr,
              list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  bits<4> rd;     // Register (output operand, tied to input)
  bits<16> imm;   // Immediate value

  bits<32> Inst;

  // First word: 0000 0010 ssss rrrr
  // where ssss = sub-opcode, rrrr = register
  let Inst{15-8} = 0b00000010;  // Format 8 identifier
  let Inst{7-4} = subop;
  let Inst{3-0} = rd;
  // Second word
  let Inst{31-16} = imm;

  let Size = 4;  // Two words
}

// Format8 variant for compare immediate (no output, just sets flags)
// Operands: (outs), (ins $rs, $imm)
class Format8_Cmp<bits<4> subop, dag outs, dag ins, string asmstr,
                  list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  bits<4> rs;     // Register to compare (input only)
  bits<16> imm;   // Immediate value

  bits<32> Inst;

  // First word: 0000 0010 ssss rrrr
  let Inst{15-8} = 0b00000010;  // Format 8 identifier
  let Inst{7-4} = subop;
  let Inst{3-0} = rs;
  // Second word
  let Inst{31-16} = imm;

  let Size = 4;  // Two words
}

//===----------------------------------------------------------------------===//
// Format 9: Internal Register Immediate (LWPI, LIMI)
// [opcode:11][N:5] + [imm:16]
//===----------------------------------------------------------------------===//

class Format9<bits<11> opcode, dag outs, dag ins, string asmstr,
              list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  bits<16> imm;   // Immediate value

  bits<32> Inst;

  // First word (low 16 bits in big-endian output)
  let Inst{15-5} = opcode;
  let Inst{4-0} = 0;          // N (unused)
  // Second word (high 16 bits in big-endian output)
  let Inst{31-16} = imm;

  let Size = 4;  // Two words
}

//===----------------------------------------------------------------------===//
// Format 11: RTWP
// [opcode:16]
//===----------------------------------------------------------------------===//

class Format11<bits<16> opcode, dag outs, dag ins, string asmstr,
               list<dag> pattern>
    : TMS9900Inst<outs, ins, asmstr, pattern> {
  bits<16> Inst = opcode;
}
