; RUN: llvm-mc -triple tms9900 -show-encoding < %s | FileCheck %s
; RUN: llvm-mc -filetype=obj -triple tms9900 < %s \
; RUN:   | llvm-objdump -d - | FileCheck --check-prefix=DISASM %s
;
; Encode+decode round-trip test for all major TMS9900 instruction formats.
; Each instruction class is represented by at least one instruction.
; Verifies that assembling then disassembling produces the original instruction.

  ; Format 1: two-operand (MOV, A, S, C, SOC, SZC, etc.)
  mov r1, r2
  a r3, r4
  s *r5, r6
  c @0x1234, r7

  ; Format 1 byte: MOVB, AB, SB, CB, SOCB, SZCB
  movb r8, r9
  cb *r1, r2

  ; Format 2: COC, CZC, XOR, MPY, DIV
  coc r3, r4
  xor r5, r6
  mpy r7, r0
  div r8, r2

  ; Format 3: single-operand (CLR, NEG, INV, INC, etc.)
  clr r9
  neg *r1
  inv r2
  swpb r3

  ; Format 4: shift (SLA, SRA, SRL, SRC)
  sla r1, 3
  sra r2, 4
  srl r3, 5
  src r4, 6

  ; Format 5: immediate (LI, AI, ANDI, ORI, CI)
  li r5, 0x1234
  ai r6, 256
  andi r7, 0x00ff
  ci r8, 0x2222

  ; Format 6: XOP
  xop r3, 4

  ; Format 7: CRU bit (LDCR, STCR)
  ldcr r5, 8
  stcr r9, 0

  ; Format 8: CRU single-bit (SBO, SBZ, TB)
  sbo 5
  sbz -1
  tb 127

  ; Format 9: misc register (STST, STWP)
  stst r1
  stwp r2

  ; Format 10: immediate-only (LWPI, LIMI)
  lwpi 0x1000
  limi 4

  ; Format 11: no-operand (RTWP, IDLE, RSET, CKOF, CKON, LREX)
  rtwp
  idle

; -- Format 1 --
; CHECK: MOV{{[ \t]+}}R1,R2{{[ \t]+}}; encoding: [0xc0,0x81]
; CHECK: A{{[ \t]+}}R3,R4{{[ \t]+}}; encoding: [0xa1,0x03]
; CHECK: S{{[ \t]+}}*R5,R6{{[ \t]+}}; encoding: [0x61,0x95]
; CHECK: C{{[ \t]+}}@0x1234,R7{{[ \t]+}}; encoding: [0x81,0xe0,0x12,0x34]
; CHECK: MOVB{{[ \t]+}}R8,R9{{[ \t]+}}; encoding: [0xd2,0x48]
; CHECK: CB{{[ \t]+}}*R1,R2{{[ \t]+}}; encoding: [0x90,0x91]

; -- Format 2 --
; CHECK: COC{{[ \t]+}}R3,R4{{[ \t]+}}; encoding: [0x21,0x03]
; CHECK: XOR{{[ \t]+}}R5,R6{{[ \t]+}}; encoding: [0x29,0x85]
; CHECK: MPY{{[ \t]+}}R7,R0{{[ \t]+}}; encoding: [0x38,0x07]
; CHECK: DIV{{[ \t]+}}R8,R2{{[ \t]+}}; encoding: [0x3c,0x88]

; -- Format 3 --
; CHECK: CLR{{[ \t]+}}R9{{[ \t]+}}; encoding: [0x04,0xc9]
; CHECK: NEG{{[ \t]+}}*R1{{[ \t]+}}; encoding: [0x05,0x11]
; CHECK: INV{{[ \t]+}}R2{{[ \t]+}}; encoding: [0x05,0x42]
; CHECK: SWPB{{[ \t]+}}R3{{[ \t]+}}; encoding: [0x06,0xc3]

; -- Format 4 --
; CHECK: SLA{{[ \t]+}}R1,3{{[ \t]+}}; encoding: [0x0a,0x31]
; CHECK: SRA{{[ \t]+}}R2,4{{[ \t]+}}; encoding: [0x08,0x42]
; CHECK: SRL{{[ \t]+}}R3,5{{[ \t]+}}; encoding: [0x09,0x53]
; CHECK: SRC{{[ \t]+}}R4,6{{[ \t]+}}; encoding: [0x0b,0x64]

; -- Format 5 --
; CHECK: LI{{[ \t]+}}R5,4660{{[ \t]+}}; encoding: [0x02,0x05,0x12,0x34]
; CHECK: AI{{[ \t]+}}R6,256{{[ \t]+}}; encoding: [0x02,0x26,0x01,0x00]
; CHECK: ANDI{{[ \t]+}}R7,255{{[ \t]+}}; encoding: [0x02,0x47,0x00,0xff]
; CHECK: CI{{[ \t]+}}R8,8738{{[ \t]+}}; encoding: [0x02,0x88,0x22,0x22]

; -- Format 6 --
; CHECK: XOP{{[ \t]+}}R3,4{{[ \t]+}}; encoding: [0x2d,0x03]

; -- Format 7 --
; CHECK: LDCR{{[ \t]+}}R5,8{{[ \t]+}}; encoding: [0x32,0x05]
; CHECK: STCR{{[ \t]+}}R9,0{{[ \t]+}}; encoding: [0x34,0x09]

; -- Format 8 --
; CHECK: SBO{{[ \t]+}}5{{[ \t]+}}; encoding: [0x1d,0x05]
; CHECK: SBZ{{[ \t]+}}-1{{[ \t]+}}; encoding: [0x1e,0xff]
; CHECK: TB{{[ \t]+}}127{{[ \t]+}}; encoding: [0x1f,0x7f]

; -- Format 9 --
; CHECK: STST{{[ \t]+}}R1{{[ \t]+}}; encoding: [0x02,0xc1]
; CHECK: STWP{{[ \t]+}}R2{{[ \t]+}}; encoding: [0x02,0xa2]

; -- Format 10 --
; CHECK: LWPI{{[ \t]+}}4096{{[ \t]+}}; encoding: [0x02,0xe0,0x10,0x00]
; CHECK: LIMI{{[ \t]+}}4{{[ \t]+}}; encoding: [0x03,0x00,0x00,0x04]

; -- Format 11 --
; CHECK: RTWP{{[ \t]+}}; encoding: [0x03,0x80]
; CHECK: IDLE{{[ \t]+}}; encoding: [0x03,0x40]

; -- Disassembly round-trip --
; DISASM: {{[0-9a-f]+}}: c0 81{{[ \t]+}}MOV{{[ \t]+}}R1,R2
; DISASM: {{[0-9a-f]+}}: a1 03{{[ \t]+}}A{{[ \t]+}}R3,R4
; DISASM: {{[0-9a-f]+}}: 61 95{{[ \t]+}}S{{[ \t]+}}*R5,R6
; DISASM: {{[0-9a-f]+}}: 81 e0 12 34{{[ \t]+}}C{{[ \t]+}}@0x1234,R7
; DISASM: {{[0-9a-f]+}}: d2 48{{[ \t]+}}MOVB{{[ \t]+}}R8,R9
; DISASM: {{[0-9a-f]+}}: 90 91{{[ \t]+}}CB{{[ \t]+}}*R1,R2
; DISASM: {{[0-9a-f]+}}: 21 03{{[ \t]+}}COC{{[ \t]+}}R3,R4
; DISASM: {{[0-9a-f]+}}: 29 85{{[ \t]+}}XOR{{[ \t]+}}R5,R6
; DISASM: {{[0-9a-f]+}}: 38 07{{[ \t]+}}MPY{{[ \t]+}}R7,R0
; DISASM: {{[0-9a-f]+}}: 3c 88{{[ \t]+}}DIV{{[ \t]+}}R8,R2
; DISASM: {{[0-9a-f]+}}: 04 c9{{[ \t]+}}CLR{{[ \t]+}}R9
; DISASM: {{[0-9a-f]+}}: 05 11{{[ \t]+}}NEG{{[ \t]+}}*R1
; DISASM: {{[0-9a-f]+}}: 05 42{{[ \t]+}}INV{{[ \t]+}}R2
; DISASM: {{[0-9a-f]+}}: 06 c3{{[ \t]+}}SWPB{{[ \t]+}}R3
; DISASM: {{[0-9a-f]+}}: 0a 31{{[ \t]+}}SLA{{[ \t]+}}R1,3
; DISASM: {{[0-9a-f]+}}: 08 42{{[ \t]+}}SRA{{[ \t]+}}R2,4
; DISASM: {{[0-9a-f]+}}: 09 53{{[ \t]+}}SRL{{[ \t]+}}R3,5
; DISASM: {{[0-9a-f]+}}: 0b 64{{[ \t]+}}SRC{{[ \t]+}}R4,6
; DISASM: {{[0-9a-f]+}}: 02 05 12 34{{[ \t]+}}LI{{[ \t]+}}R5,4660
; DISASM: {{[0-9a-f]+}}: 02 26 01 00{{[ \t]+}}AI{{[ \t]+}}R6,256
; DISASM: {{[0-9a-f]+}}: 02 47 00 ff{{[ \t]+}}ANDI{{[ \t]+}}R7,255
; DISASM: {{[0-9a-f]+}}: 02 88 22 22{{[ \t]+}}CI{{[ \t]+}}R8,8738
; DISASM: {{[0-9a-f]+}}: 2d 03{{[ \t]+}}XOP{{[ \t]+}}R3,4
; DISASM: {{[0-9a-f]+}}: 32 05{{[ \t]+}}LDCR{{[ \t]+}}R5,8
; DISASM: {{[0-9a-f]+}}: 34 09{{[ \t]+}}STCR{{[ \t]+}}R9,0
; DISASM: {{[0-9a-f]+}}: 1d 05{{[ \t]+}}SBO{{[ \t]+}}5
; DISASM: {{[0-9a-f]+}}: 1e ff{{[ \t]+}}SBZ{{[ \t]+}}-1
; DISASM: {{[0-9a-f]+}}: 1f 7f{{[ \t]+}}TB{{[ \t]+}}127
; DISASM: {{[0-9a-f]+}}: 02 c1{{[ \t]+}}STST{{[ \t]+}}R1
; DISASM: {{[0-9a-f]+}}: 02 a2{{[ \t]+}}STWP{{[ \t]+}}R2
; DISASM: {{[0-9a-f]+}}: 02 e0 10 00{{[ \t]+}}LWPI{{[ \t]+}}4096
; DISASM: {{[0-9a-f]+}}: 03 00 00 04{{[ \t]+}}LIMI{{[ \t]+}}4
; DISASM: {{[0-9a-f]+}}: 03 80{{[ \t]+}}RTWP
; DISASM: {{[0-9a-f]+}}: 03 40{{[ \t]+}}IDLE
