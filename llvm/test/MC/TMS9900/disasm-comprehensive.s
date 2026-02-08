; RUN: llvm-mc -triple tms9900 -show-encoding < %s | FileCheck %s
; RUN: llvm-mc -filetype=obj -triple tms9900 < %s \
; RUN:   | llvm-objdump -d - | FileCheck --check-prefix=DISASM %s
;
; Comprehensive assembly/disassembly round-trip test for TMS9900.
; Tests every instruction format with every applicable addressing mode.
;
; Addressing modes (Ts/Td field):
;   00 = Register direct (Rx)
;   01 = Register indirect (*Rx)
;   10 = Symbolic @addr (S/D=0) or Indexed @offset(Rx) (S/D!=0)
;   11 = Register indirect auto-increment (*Rx+)

; ===========================================================================
; FORMAT 1: Two-operand instructions
; ===========================================================================

; --- MOV: All 16 Ts x Td addressing mode combinations ---

; Reg-to-Reg
  mov r1, r2
; Indirect-to-Reg
  mov *r3, r4
; Indexed-to-Reg
  mov @4(r1), r2
; Symbolic-to-Reg
  mov @0x1234, r7
; AutoInc-to-Reg
  mov *r5+, r6
; Reg-to-Indirect
  mov r1, *r2
; Reg-to-Indexed
  mov r5, @16(r6)
; Reg-to-Symbolic
  mov r2, @0x1234
; Reg-to-AutoInc
  mov r9, *r10+
; Indirect-to-Indirect
  mov *r1, *r2
; Symbolic-to-Indirect
  mov @0x1111, *r3
; Indexed-to-Indirect
  mov @6(r4), *r5
; AutoInc-to-Indirect
  mov *r6+, *r7
; Indirect-to-Symbolic
  mov *r1, @0x2222
; Indirect-to-Indexed
  mov *r2, @8(r3)
; Indirect-to-AutoInc
  mov *r4, *r5+
; Symbolic-to-Symbolic (3 words)
  mov @0x1111, @0x2222
; Symbolic-to-Indexed (3 words)
  mov @0x1234, @8(r1)
; Indexed-to-Symbolic (3 words)
  mov @6(r2), @0x3456
; Indexed-to-Indexed (3 words)
  mov @2(r3), @4(r4)
; AutoInc-to-Symbolic
  mov *r1+, @0x3333
; AutoInc-to-Indexed
  mov *r2+, @10(r3)
; Symbolic-to-AutoInc
  mov @0x4444, *r4+
; Indexed-to-AutoInc
  mov @12(r5), *r6+
; AutoInc-to-AutoInc
  mov *r7+, *r8+

; --- MOVB: byte variant representative addressing modes ---
  movb r1, r2
  movb *r3, r4
  movb @0x1234, r5
  movb @4(r6), r7
  movb *r8+, r9
  movb r10, *r11
  movb r12, @0x5678
  movb r13, @6(r14)
  movb r15, *r1+
  movb *r2, *r3
  movb @0x1111, @0x2222

; --- A (Add): all source addressing modes + representative dest modes ---
  a r1, r2
  a *r3, r4
  a @0x1234, r5
  a @4(r6), r7
  a *r8+, r9
  a r1, *r2
  a r3, @0x1234
  a r4, @6(r5)
  a r6, *r7+
  a @0x1111, @0x2222

; --- AB (Add Byte) ---
  ab r1, r2
  ab *r3, r4
  ab @0x1234, r5
  ab @4(r6), r7
  ab *r8+, r9
  ab r1, *r2

; --- S (Subtract) ---
  s r1, r2
  s *r3, r4
  s @0x1234, r5
  s @4(r6), r7
  s *r8+, r9
  s r1, *r2
  s r3, @0x2345
  s r4, @8(r5)

; --- SB (Subtract Byte) ---
  sb r1, r2
  sb *r3, r4
  sb r5, *r6

; --- C (Compare) ---
  c r5, r6
  c *r1, r2
  c @0x1234, r3
  c @4(r5), r6
  c *r7+, r8
  c r1, *r2
  c r3, @0x1234
  c r4, @6(r5)
  c r6, *r7+
  c @0x1111, @0x2222

; --- CB (Compare Byte) ---
  cb r1, r2
  cb *r3, r4
  cb @0x1234, r5
  cb r6, *r7

; --- SOC (Set Ones Corresponding) ---
  soc r1, r2
  soc *r3, r4
  soc @0x1234, r5
  soc @4(r6), r7
  soc *r8+, r9
  soc r1, *r2
  soc r3, @0x2345
  soc r4, *r5+

; --- SOCB (SOC Byte) ---
  socb r1, r2
  socb *r3, r4
  socb r5, *r6

; --- SZC (Set Zeros Corresponding) ---
  szc r1, r2
  szc *r3, r4
  szc @0x1234, r5
  szc @4(r6), r7
  szc *r8+, r9
  szc r1, *r2
  szc r3, @0x2345
  szc r4, *r5+

; --- SZCB (SZC Byte) ---
  szcb r1, r2
  szcb *r3, r4
  szcb r5, *r6

; ===========================================================================
; FORMAT 2: Two-operand (source general, dest register)
; ===========================================================================

; --- COC ---
  coc r3, r4
  coc *r1, r2
  coc @0x1234, r3
  coc @4(r4), r5
  coc *r6+, r7

; --- CZC ---
  czc r5, r6
  czc *r1, r2
  czc @0x1234, r3
  czc @4(r4), r5
  czc *r6+, r7

; --- XOR ---
  xor r3, r4
  xor *r5, r6
  xor @0x1234, r7
  xor @4(r8), r9
  xor *r10+, r11

; --- MPY (all source addressing modes, various dest regs) ---
  mpy r3, r0
  mpy r5, r4
  mpy r1, r8
  mpy *r1, r6
  mpy @0x0100, r2
  mpy @4(r3), r4
  mpy *r5+, r6

; --- DIV (all source addressing modes, various dest regs) ---
  div r2, r0
  div r3, r4
  div *r5, r2
  div @0x0200, r0
  div @8(r9), r4
  div *r6+, r8

; ===========================================================================
; FORMAT 3: Single-operand instructions
; ===========================================================================

; --- CLR ---
  clr r1
  clr *r2
  clr *r3+
  clr @0x1234
  clr @4(r5)

; --- SETO ---
  seto r1
  seto *r2
  seto *r3+
  seto @0x1234
  seto @4(r5)

; --- NEG ---
  neg r1
  neg *r2
  neg *r3+
  neg @0x1234
  neg @4(r5)

; --- INV ---
  inv r1
  inv *r2
  inv *r3+
  inv @0x1234
  inv @4(r5)

; --- INC ---
  inc r1
  inc *r2
  inc *r3+
  inc @0x1234
  inc @4(r5)

; --- INCT ---
  inct r1
  inct *r2
  inct *r3+
  inct @0x1234
  inct @4(r5)

; --- DEC ---
  dec r1
  dec *r2
  dec *r3+
  dec @0x1234
  dec @4(r5)

; --- DECT ---
  dect r1
  dect *r2
  dect *r3+
  dect @0x1234
  dect @4(r5)

; --- ABS ---
  abs r1
  abs *r2
  abs *r3+
  abs @0x1234
  abs @4(r5)

; --- SWPB ---
  swpb r1
  swpb *r2
  swpb *r3+
  swpb @0x1234
  swpb @4(r5)

; --- B (Branch - single operand format) ---
  b r11
  b *r11

; --- BL (Branch and Link) ---
  bl r13
  bl *r13

; --- BLWP (Branch and Load Workspace Pointer) ---
  blwp r3
  blwp *r2
  blwp *r4+
  blwp @0x0040
  blwp @8(r5)

; --- X (Execute) ---
  x r8
  x *r7
  x *r10+
  x @0x5678
  x @8(r9)

; ===========================================================================
; FORMAT 4: CRU multi-bit (LDCR, STCR)
; ===========================================================================

; --- LDCR ---
  ldcr r5, 0
  ldcr r1, 8
  ldcr *r6, 8
  ldcr *r7+, 4
  ldcr @0x2000, 2
  ldcr @6(r8), 1

; --- STCR ---
  stcr r9, 0
  stcr r1, 7
  stcr *r10, 7
  stcr *r11+, 4
  stcr @0x3000, 3
  stcr @10(r12), 2

; ===========================================================================
; XOP (Extended Operation)
; ===========================================================================
  xop r1, 3
  xop *r2, 4
  xop *r3+, 5
  xop @0x1234, 6
  xop @8(r4), 7

; ===========================================================================
; FORMAT 5: CRU single-bit (SBO, SBZ, TB)
; ===========================================================================
  sbo 5
  sbo 0
  sbo -1
  sbz -1
  sbz 0
  sbz 127
  tb 127
  tb 0
  tb -128

; ===========================================================================
; FORMAT 6: Jump (PC-relative) - forward and backward
; ===========================================================================
back:
  nop
  jeq fwd
  jne fwd
  jgt fwd
  jlt fwd
  jh fwd
  jhe fwd
  jl fwd
  jle fwd
  joc fwd
  jnc fwd
  jno fwd
  jop fwd
  jmp fwd
  jmp back
fwd:

; ===========================================================================
; FORMAT 7: Shift instructions
; ===========================================================================

; Constant counts (1-15)
  sla r1, 1
  sla r2, 8
  sla r3, 15
  sra r4, 1
  sra r5, 8
  sra r6, 15
  srl r7, 1
  srl r8, 8
  srl r9, 15
  src r10, 1
  src r11, 8
  src r12, 15

; R0 count (count field = 0)
  sla r1, 0
  sra r2, 0
  srl r3, 0
  src r4, 0

; ===========================================================================
; FORMAT 8: Immediate register (LI, AI, ANDI, ORI, CI)
; ===========================================================================
  li r1, 0x1234
  li r5, 0
  li r15, 0xFFFF
  ai r2, 256
  ai r3, -1
  andi r4, 0x00FF
  andi r5, 0xFF00
  ori r6, 0x0F0F
  ci r7, 0x2222
  ci r8, 0

; ===========================================================================
; FORMAT 9/10: Internal register (LWPI, LIMI, STST, STWP)
; ===========================================================================
  lwpi 0x1000
  limi 4
  stst r1
  stwp r2

; ===========================================================================
; FORMAT 11: No-operand instructions
; ===========================================================================
  rtwp
  idle
  rset
  ckof
  ckon
  lrex

; ===========================================================================
; CHECK lines - Assembly encoding verification
; ===========================================================================

; --- MOV addressing mode combinations ---
; CHECK: MOV{{[ \t]+}}R1,R2{{[ \t]+}}; encoding: [0xc0,0x81]
; CHECK: MOV{{[ \t]+}}*R3,R4{{[ \t]+}}; encoding: [0xc1,0x13]
; CHECK: MOV{{[ \t]+}}@4(R1),R2{{[ \t]+}}; encoding: [0xc0,0xa1,0x00,0x04]
; CHECK: MOV{{[ \t]+}}@0x1234,R7{{[ \t]+}}; encoding: [0xc1,0xe0,0x12,0x34]
; CHECK: MOV{{[ \t]+}}*R5+,R6{{[ \t]+}}; encoding: [0xc1,0xb5]
; CHECK: MOV{{[ \t]+}}R1,*R2{{[ \t]+}}; encoding: [0xc4,0x81]
; CHECK: MOV{{[ \t]+}}R5,@16(R6){{[ \t]+}}; encoding: [0xc9,0x85,0x00,0x10]
; CHECK: MOV{{[ \t]+}}R2,@0x1234{{[ \t]+}}; encoding: [0xc8,0x02,0x12,0x34]
; CHECK: MOV{{[ \t]+}}R9,*R10+{{[ \t]+}}; encoding: [0xce,0x89]
; CHECK: MOV{{[ \t]+}}*R1,*R2{{[ \t]+}}; encoding: [0xc4,0x91]
; CHECK: MOV{{[ \t]+}}@0x1111,*R3{{[ \t]+}}; encoding: [0xc4,0xe0,0x11,0x11]
; CHECK: MOV{{[ \t]+}}@6(R4),*R5{{[ \t]+}}; encoding: [0xc5,0x64,0x00,0x06]
; CHECK: MOV{{[ \t]+}}*R6+,*R7{{[ \t]+}}; encoding: [0xc5,0xf6]
; CHECK: MOV{{[ \t]+}}*R1,@0x2222{{[ \t]+}}; encoding: [0xc8,0x11,0x22,0x22]
; CHECK: MOV{{[ \t]+}}*R2,@8(R3){{[ \t]+}}; encoding: [0xc8,0xd2,0x00,0x08]
; CHECK: MOV{{[ \t]+}}*R4,*R5+{{[ \t]+}}; encoding: [0xcd,0x54]
; CHECK: MOV{{[ \t]+}}@0x1111,@0x2222{{[ \t]+}}; encoding: [0xc8,0x20,0x11,0x11,0x22,0x22]
; CHECK: MOV{{[ \t]+}}@0x1234,@8(R1){{[ \t]+}}; encoding: [0xc8,0x60,0x12,0x34,0x00,0x08]
; CHECK: MOV{{[ \t]+}}@6(R2),@0x3456{{[ \t]+}}; encoding: [0xc8,0x22,0x00,0x06,0x34,0x56]
; CHECK: MOV{{[ \t]+}}@2(R3),@4(R4){{[ \t]+}}; encoding: [0xc9,0x23,0x00,0x02,0x00,0x04]
; CHECK: MOV{{[ \t]+}}*R1+,@0x3333{{[ \t]+}}; encoding: [0xc8,0x31,0x33,0x33]
; CHECK: MOV{{[ \t]+}}*R2+,@10(R3){{[ \t]+}}; encoding: [0xc8,0xf2,0x00,0x0a]
; CHECK: MOV{{[ \t]+}}@0x4444,*R4+{{[ \t]+}}; encoding: [0xcd,0x20,0x44,0x44]
; CHECK: MOV{{[ \t]+}}@12(R5),*R6+{{[ \t]+}}; encoding: [0xcd,0xa5,0x00,0x0c]
; CHECK: MOV{{[ \t]+}}*R7+,*R8+{{[ \t]+}}; encoding: [0xce,0x37]

; --- MOVB ---
; CHECK: MOVB{{[ \t]+}}R1,R2{{[ \t]+}}; encoding: [0xd0,0x81]
; CHECK: MOVB{{[ \t]+}}*R3,R4{{[ \t]+}}; encoding: [0xd1,0x13]
; CHECK: MOVB{{[ \t]+}}@0x1234,R5{{[ \t]+}}; encoding: [0xd1,0x60,0x12,0x34]
; CHECK: MOVB{{[ \t]+}}@4(R6),R7{{[ \t]+}}; encoding: [0xd1,0xe6,0x00,0x04]
; CHECK: MOVB{{[ \t]+}}*R8+,R9{{[ \t]+}}; encoding: [0xd2,0x78]
; CHECK: MOVB{{[ \t]+}}R10,*R11{{[ \t]+}}; encoding: [0xd6,0xca]
; CHECK: MOVB{{[ \t]+}}R12,@0x5678{{[ \t]+}}; encoding: [0xd8,0x0c,0x56,0x78]
; CHECK: MOVB{{[ \t]+}}R13,@6(R14){{[ \t]+}}; encoding: [0xdb,0x8d,0x00,0x06]
; CHECK: MOVB{{[ \t]+}}R15,*R1+{{[ \t]+}}; encoding: [0xdc,0x4f]
; CHECK: MOVB{{[ \t]+}}*R2,*R3{{[ \t]+}}; encoding: [0xd4,0xd2]
; CHECK: MOVB{{[ \t]+}}@0x1111,@0x2222{{[ \t]+}}; encoding: [0xd8,0x20,0x11,0x11,0x22,0x22]

; --- A ---
; CHECK: A{{[ \t]+}}R1,R2{{[ \t]+}}; encoding: [0xa0,0x81]
; CHECK: A{{[ \t]+}}*R3,R4{{[ \t]+}}; encoding: [0xa1,0x13]
; CHECK: A{{[ \t]+}}@0x1234,R5{{[ \t]+}}; encoding: [0xa1,0x60,0x12,0x34]
; CHECK: A{{[ \t]+}}@4(R6),R7{{[ \t]+}}; encoding: [0xa1,0xe6,0x00,0x04]
; CHECK: A{{[ \t]+}}*R8+,R9{{[ \t]+}}; encoding: [0xa2,0x78]
; CHECK: A{{[ \t]+}}R1,*R2{{[ \t]+}}; encoding: [0xa4,0x81]
; CHECK: A{{[ \t]+}}R3,@0x1234{{[ \t]+}}; encoding: [0xa8,0x03,0x12,0x34]
; CHECK: A{{[ \t]+}}R4,@6(R5){{[ \t]+}}; encoding: [0xa9,0x44,0x00,0x06]
; CHECK: A{{[ \t]+}}R6,*R7+{{[ \t]+}}; encoding: [0xad,0xc6]
; CHECK: A{{[ \t]+}}@0x1111,@0x2222{{[ \t]+}}; encoding: [0xa8,0x20,0x11,0x11,0x22,0x22]

; --- AB ---
; CHECK: AB{{[ \t]+}}R1,R2{{[ \t]+}}; encoding: [0xb0,0x81]
; CHECK: AB{{[ \t]+}}*R3,R4{{[ \t]+}}; encoding: [0xb1,0x13]
; CHECK: AB{{[ \t]+}}@0x1234,R5{{[ \t]+}}; encoding: [0xb1,0x60,0x12,0x34]
; CHECK: AB{{[ \t]+}}@4(R6),R7{{[ \t]+}}; encoding: [0xb1,0xe6,0x00,0x04]
; CHECK: AB{{[ \t]+}}*R8+,R9{{[ \t]+}}; encoding: [0xb2,0x78]
; CHECK: AB{{[ \t]+}}R1,*R2{{[ \t]+}}; encoding: [0xb4,0x81]

; --- S ---
; CHECK: S{{[ \t]+}}R1,R2{{[ \t]+}}; encoding: [0x60,0x81]
; CHECK: S{{[ \t]+}}*R3,R4{{[ \t]+}}; encoding: [0x61,0x13]
; CHECK: S{{[ \t]+}}@0x1234,R5{{[ \t]+}}; encoding: [0x61,0x60,0x12,0x34]
; CHECK: S{{[ \t]+}}@4(R6),R7{{[ \t]+}}; encoding: [0x61,0xe6,0x00,0x04]
; CHECK: S{{[ \t]+}}*R8+,R9{{[ \t]+}}; encoding: [0x62,0x78]
; CHECK: S{{[ \t]+}}R1,*R2{{[ \t]+}}; encoding: [0x64,0x81]
; CHECK: S{{[ \t]+}}R3,@0x2345{{[ \t]+}}; encoding: [0x68,0x03,0x23,0x45]
; CHECK: S{{[ \t]+}}R4,@8(R5){{[ \t]+}}; encoding: [0x69,0x44,0x00,0x08]

; --- SB ---
; CHECK: SB{{[ \t]+}}R1,R2{{[ \t]+}}; encoding: [0x70,0x81]
; CHECK: SB{{[ \t]+}}*R3,R4{{[ \t]+}}; encoding: [0x71,0x13]
; CHECK: SB{{[ \t]+}}R5,*R6{{[ \t]+}}; encoding: [0x75,0x85]

; --- C ---
; CHECK: C{{[ \t]+}}R5,R6{{[ \t]+}}; encoding: [0x81,0x46]
; CHECK: C{{[ \t]+}}*R1,R2{{[ \t]+}}; encoding: [0x80,0x91]
; CHECK: C{{[ \t]+}}@0x1234,R3{{[ \t]+}}; encoding: [0x80,0xe0,0x12,0x34]
; CHECK: C{{[ \t]+}}@4(R5),R6{{[ \t]+}}; encoding: [0x81,0xa5,0x00,0x04]
; CHECK: C{{[ \t]+}}*R7+,R8{{[ \t]+}}; encoding: [0x82,0x37]
; CHECK: C{{[ \t]+}}R1,*R2{{[ \t]+}}; encoding: [0x84,0x81]
; CHECK: C{{[ \t]+}}R3,@0x1234{{[ \t]+}}; encoding: [0x88,0x03,0x12,0x34]
; CHECK: C{{[ \t]+}}R4,@6(R5){{[ \t]+}}; encoding: [0x89,0x44,0x00,0x06]
; CHECK: C{{[ \t]+}}R6,*R7+{{[ \t]+}}; encoding: [0x8d,0xc6]
; CHECK: C{{[ \t]+}}@0x1111,@0x2222{{[ \t]+}}; encoding: [0x88,0x20,0x11,0x11,0x22,0x22]

; --- CB ---
; CHECK: CB{{[ \t]+}}R1,R2{{[ \t]+}}; encoding: [0x90,0x42]
; CHECK: CB{{[ \t]+}}*R3,R4{{[ \t]+}}; encoding: [0x91,0x13]
; CHECK: CB{{[ \t]+}}@0x1234,R5{{[ \t]+}}; encoding: [0x91,0x60,0x12,0x34]
; CHECK: CB{{[ \t]+}}R6,*R7{{[ \t]+}}; encoding: [0x95,0xc6]

; --- SOC ---
; CHECK: SOC{{[ \t]+}}R1,R2{{[ \t]+}}; encoding: [0xe0,0x81]
; CHECK: SOC{{[ \t]+}}*R3,R4{{[ \t]+}}; encoding: [0xe1,0x13]
; CHECK: SOC{{[ \t]+}}@0x1234,R5{{[ \t]+}}; encoding: [0xe1,0x60,0x12,0x34]
; CHECK: SOC{{[ \t]+}}@4(R6),R7{{[ \t]+}}; encoding: [0xe1,0xe6,0x00,0x04]
; CHECK: SOC{{[ \t]+}}*R8+,R9{{[ \t]+}}; encoding: [0xe2,0x78]
; CHECK: SOC{{[ \t]+}}R1,*R2{{[ \t]+}}; encoding: [0xe4,0x81]
; CHECK: SOC{{[ \t]+}}R3,@0x2345{{[ \t]+}}; encoding: [0xe8,0x03,0x23,0x45]
; CHECK: SOC{{[ \t]+}}R4,*R5+{{[ \t]+}}; encoding: [0xed,0x44]

; --- SOCB ---
; CHECK: SOCB{{[ \t]+}}R1,R2{{[ \t]+}}; encoding: [0xf0,0x81]
; CHECK: SOCB{{[ \t]+}}*R3,R4{{[ \t]+}}; encoding: [0xf1,0x13]
; CHECK: SOCB{{[ \t]+}}R5,*R6{{[ \t]+}}; encoding: [0xf5,0x85]

; --- SZC ---
; CHECK: SZC{{[ \t]+}}R1,R2{{[ \t]+}}; encoding: [0x40,0x81]
; CHECK: SZC{{[ \t]+}}*R3,R4{{[ \t]+}}; encoding: [0x41,0x13]
; CHECK: SZC{{[ \t]+}}@0x1234,R5{{[ \t]+}}; encoding: [0x41,0x60,0x12,0x34]
; CHECK: SZC{{[ \t]+}}@4(R6),R7{{[ \t]+}}; encoding: [0x41,0xe6,0x00,0x04]
; CHECK: SZC{{[ \t]+}}*R8+,R9{{[ \t]+}}; encoding: [0x42,0x78]
; CHECK: SZC{{[ \t]+}}R1,*R2{{[ \t]+}}; encoding: [0x44,0x81]
; CHECK: SZC{{[ \t]+}}R3,@0x2345{{[ \t]+}}; encoding: [0x48,0x03,0x23,0x45]
; CHECK: SZC{{[ \t]+}}R4,*R5+{{[ \t]+}}; encoding: [0x4d,0x44]

; --- SZCB ---
; CHECK: SZCB{{[ \t]+}}R1,R2{{[ \t]+}}; encoding: [0x50,0x81]
; CHECK: SZCB{{[ \t]+}}*R3,R4{{[ \t]+}}; encoding: [0x51,0x13]
; CHECK: SZCB{{[ \t]+}}R5,*R6{{[ \t]+}}; encoding: [0x55,0x85]

; --- Format 2: COC ---
; CHECK: COC{{[ \t]+}}R3,R4{{[ \t]+}}; encoding: [0x21,0x03]
; CHECK: COC{{[ \t]+}}*R1,R2{{[ \t]+}}; encoding: [0x20,0x91]
; CHECK: COC{{[ \t]+}}@0x1234,R3{{[ \t]+}}; encoding: [0x20,0xe0,0x12,0x34]
; CHECK: COC{{[ \t]+}}@4(R4),R5{{[ \t]+}}; encoding: [0x21,0x64,0x00,0x04]
; CHECK: COC{{[ \t]+}}*R6+,R7{{[ \t]+}}; encoding: [0x21,0xf6]

; --- CZC ---
; CHECK: CZC{{[ \t]+}}R5,R6{{[ \t]+}}; encoding: [0x25,0x85]
; CHECK: CZC{{[ \t]+}}*R1,R2{{[ \t]+}}; encoding: [0x24,0x91]
; CHECK: CZC{{[ \t]+}}@0x1234,R3{{[ \t]+}}; encoding: [0x24,0xe0,0x12,0x34]
; CHECK: CZC{{[ \t]+}}@4(R4),R5{{[ \t]+}}; encoding: [0x25,0x64,0x00,0x04]
; CHECK: CZC{{[ \t]+}}*R6+,R7{{[ \t]+}}; encoding: [0x25,0xf6]

; --- XOR ---
; CHECK: XOR{{[ \t]+}}R3,R4{{[ \t]+}}; encoding: [0x29,0x03]
; CHECK: XOR{{[ \t]+}}*R5,R6{{[ \t]+}}; encoding: [0x29,0x95]
; CHECK: XOR{{[ \t]+}}@0x1234,R7{{[ \t]+}}; encoding: [0x29,0xe0,0x12,0x34]
; CHECK: XOR{{[ \t]+}}@4(R8),R9{{[ \t]+}}; encoding: [0x2a,0x68,0x00,0x04]
; CHECK: XOR{{[ \t]+}}*R10+,R11{{[ \t]+}}; encoding: [0x2a,0xfa]

; --- MPY ---
; CHECK: MPY{{[ \t]+}}R3,R0{{[ \t]+}}; encoding: [0x38,0x03]
; CHECK: MPY{{[ \t]+}}R5,R4{{[ \t]+}}; encoding: [0x39,0x05]
; CHECK: MPY{{[ \t]+}}R1,R8{{[ \t]+}}; encoding: [0x3a,0x01]
; CHECK: MPY{{[ \t]+}}*R1,R6{{[ \t]+}}; encoding: [0x39,0x91]
; CHECK: MPY{{[ \t]+}}@0x0100,R2{{[ \t]+}}; encoding: [0x38,0xa0,0x01,0x00]
; CHECK: MPY{{[ \t]+}}@4(R3),R4{{[ \t]+}}; encoding: [0x39,0x23,0x00,0x04]
; CHECK: MPY{{[ \t]+}}*R5+,R6{{[ \t]+}}; encoding: [0x39,0xb5]

; --- DIV ---
; CHECK: DIV{{[ \t]+}}R2,R0{{[ \t]+}}; encoding: [0x3c,0x02]
; CHECK: DIV{{[ \t]+}}R3,R4{{[ \t]+}}; encoding: [0x3d,0x03]
; CHECK: DIV{{[ \t]+}}*R5,R2{{[ \t]+}}; encoding: [0x3c,0x95]
; CHECK: DIV{{[ \t]+}}@0x0200,R0{{[ \t]+}}; encoding: [0x3c,0x20,0x02,0x00]
; CHECK: DIV{{[ \t]+}}@8(R9),R4{{[ \t]+}}; encoding: [0x3d,0x29,0x00,0x08]
; CHECK: DIV{{[ \t]+}}*R6+,R8{{[ \t]+}}; encoding: [0x3e,0x36]

; --- Format 3: CLR ---
; CHECK: CLR{{[ \t]+}}R1{{[ \t]+}}; encoding: [0x04,0xc1]
; CHECK: CLR{{[ \t]+}}*R2{{[ \t]+}}; encoding: [0x04,0xd2]
; CHECK: CLR{{[ \t]+}}*R3+{{[ \t]+}}; encoding: [0x04,0xf3]
; CHECK: CLR{{[ \t]+}}@0x1234{{[ \t]+}}; encoding: [0x04,0xe0,0x12,0x34]
; CHECK: CLR{{[ \t]+}}@4(R5){{[ \t]+}}; encoding: [0x04,0xe5,0x00,0x04]

; --- SETO ---
; CHECK: SETO{{[ \t]+}}R1{{[ \t]+}}; encoding: [0x07,0x01]
; CHECK: SETO{{[ \t]+}}*R2{{[ \t]+}}; encoding: [0x07,0x12]
; CHECK: SETO{{[ \t]+}}*R3+{{[ \t]+}}; encoding: [0x07,0x33]
; CHECK: SETO{{[ \t]+}}@0x1234{{[ \t]+}}; encoding: [0x07,0x20,0x12,0x34]
; CHECK: SETO{{[ \t]+}}@4(R5){{[ \t]+}}; encoding: [0x07,0x25,0x00,0x04]

; --- NEG ---
; CHECK: NEG{{[ \t]+}}R1{{[ \t]+}}; encoding: [0x05,0x01]
; CHECK: NEG{{[ \t]+}}*R2{{[ \t]+}}; encoding: [0x05,0x12]
; CHECK: NEG{{[ \t]+}}*R3+{{[ \t]+}}; encoding: [0x05,0x33]
; CHECK: NEG{{[ \t]+}}@0x1234{{[ \t]+}}; encoding: [0x05,0x20,0x12,0x34]
; CHECK: NEG{{[ \t]+}}@4(R5){{[ \t]+}}; encoding: [0x05,0x25,0x00,0x04]

; --- INV ---
; CHECK: INV{{[ \t]+}}R1{{[ \t]+}}; encoding: [0x05,0x41]
; CHECK: INV{{[ \t]+}}*R2{{[ \t]+}}; encoding: [0x05,0x52]
; CHECK: INV{{[ \t]+}}*R3+{{[ \t]+}}; encoding: [0x05,0x73]
; CHECK: INV{{[ \t]+}}@0x1234{{[ \t]+}}; encoding: [0x05,0x60,0x12,0x34]
; CHECK: INV{{[ \t]+}}@4(R5){{[ \t]+}}; encoding: [0x05,0x65,0x00,0x04]

; --- INC ---
; CHECK: INC{{[ \t]+}}R1{{[ \t]+}}; encoding: [0x05,0x81]
; CHECK: INC{{[ \t]+}}*R2{{[ \t]+}}; encoding: [0x05,0x92]
; CHECK: INC{{[ \t]+}}*R3+{{[ \t]+}}; encoding: [0x05,0xb3]
; CHECK: INC{{[ \t]+}}@0x1234{{[ \t]+}}; encoding: [0x05,0xa0,0x12,0x34]
; CHECK: INC{{[ \t]+}}@4(R5){{[ \t]+}}; encoding: [0x05,0xa5,0x00,0x04]

; --- INCT ---
; CHECK: INCT{{[ \t]+}}R1{{[ \t]+}}; encoding: [0x05,0xc1]
; CHECK: INCT{{[ \t]+}}*R2{{[ \t]+}}; encoding: [0x05,0xd2]
; CHECK: INCT{{[ \t]+}}*R3+{{[ \t]+}}; encoding: [0x05,0xf3]
; CHECK: INCT{{[ \t]+}}@0x1234{{[ \t]+}}; encoding: [0x05,0xe0,0x12,0x34]
; CHECK: INCT{{[ \t]+}}@4(R5){{[ \t]+}}; encoding: [0x05,0xe5,0x00,0x04]

; --- DEC ---
; CHECK: DEC{{[ \t]+}}R1{{[ \t]+}}; encoding: [0x06,0x01]
; CHECK: DEC{{[ \t]+}}*R2{{[ \t]+}}; encoding: [0x06,0x12]
; CHECK: DEC{{[ \t]+}}*R3+{{[ \t]+}}; encoding: [0x06,0x33]
; CHECK: DEC{{[ \t]+}}@0x1234{{[ \t]+}}; encoding: [0x06,0x20,0x12,0x34]
; CHECK: DEC{{[ \t]+}}@4(R5){{[ \t]+}}; encoding: [0x06,0x25,0x00,0x04]

; --- DECT ---
; CHECK: DECT{{[ \t]+}}R1{{[ \t]+}}; encoding: [0x06,0x41]
; CHECK: DECT{{[ \t]+}}*R2{{[ \t]+}}; encoding: [0x06,0x52]
; CHECK: DECT{{[ \t]+}}*R3+{{[ \t]+}}; encoding: [0x06,0x73]
; CHECK: DECT{{[ \t]+}}@0x1234{{[ \t]+}}; encoding: [0x06,0x60,0x12,0x34]
; CHECK: DECT{{[ \t]+}}@4(R5){{[ \t]+}}; encoding: [0x06,0x65,0x00,0x04]

; --- ABS ---
; CHECK: ABS{{[ \t]+}}R1{{[ \t]+}}; encoding: [0x07,0x41]
; CHECK: ABS{{[ \t]+}}*R2{{[ \t]+}}; encoding: [0x07,0x52]
; CHECK: ABS{{[ \t]+}}*R3+{{[ \t]+}}; encoding: [0x07,0x73]
; CHECK: ABS{{[ \t]+}}@0x1234{{[ \t]+}}; encoding: [0x07,0x60,0x12,0x34]
; CHECK: ABS{{[ \t]+}}@4(R5){{[ \t]+}}; encoding: [0x07,0x65,0x00,0x04]

; --- SWPB ---
; CHECK: SWPB{{[ \t]+}}R1{{[ \t]+}}; encoding: [0x06,0xc1]
; CHECK: SWPB{{[ \t]+}}*R2{{[ \t]+}}; encoding: [0x06,0xd2]
; CHECK: SWPB{{[ \t]+}}*R3+{{[ \t]+}}; encoding: [0x06,0xf3]
; CHECK: SWPB{{[ \t]+}}@0x1234{{[ \t]+}}; encoding: [0x06,0xe0,0x12,0x34]
; CHECK: SWPB{{[ \t]+}}@4(R5){{[ \t]+}}; encoding: [0x06,0xe5,0x00,0x04]

; --- B ---
; CHECK: B{{[ \t]+}}R11{{[ \t]+}}; encoding: [0x04,0x4b]
; CHECK: B{{[ \t]+}}*R11{{[ \t]+}}; encoding: [0x04,0x5b]

; --- BL ---
; CHECK: BL{{[ \t]+}}R13{{[ \t]+}}; encoding: [0x06,0x8d]
; CHECK: BL{{[ \t]+}}*R13{{[ \t]+}}; encoding: [0x06,0x9d]

; --- BLWP ---
; CHECK: BLWP{{[ \t]+}}R3{{[ \t]+}}; encoding: [0x04,0x03]
; CHECK: BLWP{{[ \t]+}}*R2{{[ \t]+}}; encoding: [0x04,0x12]
; CHECK: BLWP{{[ \t]+}}*R4+{{[ \t]+}}; encoding: [0x04,0x34]
; CHECK: BLWP{{[ \t]+}}@0x0040{{[ \t]+}}; encoding: [0x04,0x20,0x00,0x40]
; CHECK: BLWP{{[ \t]+}}@8(R5){{[ \t]+}}; encoding: [0x04,0x25,0x00,0x08]

; --- X ---
; CHECK: X{{[ \t]+}}R8{{[ \t]+}}; encoding: [0x04,0x88]
; CHECK: X{{[ \t]+}}*R7{{[ \t]+}}; encoding: [0x04,0x97]
; CHECK: X{{[ \t]+}}*R10+{{[ \t]+}}; encoding: [0x04,0xba]
; CHECK: X{{[ \t]+}}@0x5678{{[ \t]+}}; encoding: [0x04,0xa0,0x56,0x78]
; CHECK: X{{[ \t]+}}@8(R9){{[ \t]+}}; encoding: [0x04,0xa9,0x00,0x08]

; --- LDCR ---
; CHECK: LDCR{{[ \t]+}}R5,0{{[ \t]+}}; encoding: [0x30,0x05]
; CHECK: LDCR{{[ \t]+}}R1,8{{[ \t]+}}; encoding: [0x32,0x01]
; CHECK: LDCR{{[ \t]+}}*R6,8{{[ \t]+}}; encoding: [0x32,0x16]
; CHECK: LDCR{{[ \t]+}}*R7+,4{{[ \t]+}}; encoding: [0x31,0x37]
; CHECK: LDCR{{[ \t]+}}@0x2000,2{{[ \t]+}}; encoding: [0x30,0xa0,0x20,0x00]
; CHECK: LDCR{{[ \t]+}}@6(R8),1{{[ \t]+}}; encoding: [0x30,0x68,0x00,0x06]

; --- STCR ---
; CHECK: STCR{{[ \t]+}}R9,0{{[ \t]+}}; encoding: [0x34,0x09]
; CHECK: STCR{{[ \t]+}}R1,7{{[ \t]+}}; encoding: [0x35,0xc1]
; CHECK: STCR{{[ \t]+}}*R10,7{{[ \t]+}}; encoding: [0x35,0xda]
; CHECK: STCR{{[ \t]+}}*R11+,4{{[ \t]+}}; encoding: [0x35,0x3b]
; CHECK: STCR{{[ \t]+}}@0x3000,3{{[ \t]+}}; encoding: [0x34,0xe0,0x30,0x00]
; CHECK: STCR{{[ \t]+}}@10(R12),2{{[ \t]+}}; encoding: [0x34,0xac,0x00,0x0a]

; --- XOP ---
; CHECK: XOP{{[ \t]+}}R1,3{{[ \t]+}}; encoding: [0x2c,0xc1]
; CHECK: XOP{{[ \t]+}}*R2,4{{[ \t]+}}; encoding: [0x2d,0x12]
; CHECK: XOP{{[ \t]+}}*R3+,5{{[ \t]+}}; encoding: [0x2d,0x73]
; CHECK: XOP{{[ \t]+}}@0x1234,6{{[ \t]+}}; encoding: [0x2d,0xa0,0x12,0x34]
; CHECK: XOP{{[ \t]+}}@8(R4),7{{[ \t]+}}; encoding: [0x2d,0xe4,0x00,0x08]

; --- SBO, SBZ, TB ---
; CHECK: SBO{{[ \t]+}}5{{[ \t]+}}; encoding: [0x1d,0x05]
; CHECK: SBO{{[ \t]+}}0{{[ \t]+}}; encoding: [0x1d,0x00]
; CHECK: SBO{{[ \t]+}}-1{{[ \t]+}}; encoding: [0x1d,0xff]
; CHECK: SBZ{{[ \t]+}}-1{{[ \t]+}}; encoding: [0x1e,0xff]
; CHECK: SBZ{{[ \t]+}}0{{[ \t]+}}; encoding: [0x1e,0x00]
; CHECK: SBZ{{[ \t]+}}127{{[ \t]+}}; encoding: [0x1e,0x7f]
; CHECK: TB{{[ \t]+}}127{{[ \t]+}}; encoding: [0x1f,0x7f]
; CHECK: TB{{[ \t]+}}0{{[ \t]+}}; encoding: [0x1f,0x00]
; CHECK: TB{{[ \t]+}}-128{{[ \t]+}}; encoding: [0x1f,0x80]

; --- Jumps ---
; CHECK: NOP{{[ \t]+}}; encoding: [0x10,0x00]
; CHECK: JEQ{{[ \t]+}}fwd
; CHECK: JNE{{[ \t]+}}fwd
; CHECK: JGT{{[ \t]+}}fwd
; CHECK: JLT{{[ \t]+}}fwd
; CHECK: JH{{[ \t]+}}fwd
; CHECK: JHE{{[ \t]+}}fwd
; CHECK: JL{{[ \t]+}}fwd
; CHECK: JLE{{[ \t]+}}fwd
; CHECK: JOC{{[ \t]+}}fwd
; CHECK: JNC{{[ \t]+}}fwd
; CHECK: JNO{{[ \t]+}}fwd
; CHECK: JOP{{[ \t]+}}fwd
; CHECK: JMP{{[ \t]+}}fwd
; CHECK: JMP{{[ \t]+}}back

; --- Shifts ---
; CHECK: SLA{{[ \t]+}}R1,1{{[ \t]+}}; encoding: [0x0a,0x11]
; CHECK: SLA{{[ \t]+}}R2,8{{[ \t]+}}; encoding: [0x0a,0x82]
; CHECK: SLA{{[ \t]+}}R3,15{{[ \t]+}}; encoding: [0x0a,0xf3]
; CHECK: SRA{{[ \t]+}}R4,1{{[ \t]+}}; encoding: [0x08,0x14]
; CHECK: SRA{{[ \t]+}}R5,8{{[ \t]+}}; encoding: [0x08,0x85]
; CHECK: SRA{{[ \t]+}}R6,15{{[ \t]+}}; encoding: [0x08,0xf6]
; CHECK: SRL{{[ \t]+}}R7,1{{[ \t]+}}; encoding: [0x09,0x17]
; CHECK: SRL{{[ \t]+}}R8,8{{[ \t]+}}; encoding: [0x09,0x88]
; CHECK: SRL{{[ \t]+}}R9,15{{[ \t]+}}; encoding: [0x09,0xf9]
; CHECK: SRC{{[ \t]+}}R10,1{{[ \t]+}}; encoding: [0x0b,0x1a]
; CHECK: SRC{{[ \t]+}}R11,8{{[ \t]+}}; encoding: [0x0b,0x8b]
; CHECK: SRC{{[ \t]+}}R12,15{{[ \t]+}}; encoding: [0x0b,0xfc]
; CHECK: SLA{{[ \t]+}}R1,0{{[ \t]+}}; encoding: [0x0a,0x01]
; CHECK: SRA{{[ \t]+}}R2,0{{[ \t]+}}; encoding: [0x08,0x02]
; CHECK: SRL{{[ \t]+}}R3,0{{[ \t]+}}; encoding: [0x09,0x03]
; CHECK: SRC{{[ \t]+}}R4,0{{[ \t]+}}; encoding: [0x0b,0x04]

; --- Immediate ---
; CHECK: LI{{[ \t]+}}R1,4660{{[ \t]+}}; encoding: [0x02,0x01,0x12,0x34]
; CHECK: LI{{[ \t]+}}R5,0{{[ \t]+}}; encoding: [0x02,0x05,0x00,0x00]
; CHECK: LI{{[ \t]+}}R15,65535{{[ \t]+}}; encoding: [0x02,0x0f,0xff,0xff]
; CHECK: AI{{[ \t]+}}R2,256{{[ \t]+}}; encoding: [0x02,0x22,0x01,0x00]
; CHECK: AI{{[ \t]+}}R3,-1{{[ \t]+}}; encoding: [0x02,0x23,0xff,0xff]
; CHECK: ANDI{{[ \t]+}}R4,255{{[ \t]+}}; encoding: [0x02,0x44,0x00,0xff]
; CHECK: ANDI{{[ \t]+}}R5,65280{{[ \t]+}}; encoding: [0x02,0x45,0xff,0x00]
; CHECK: ORI{{[ \t]+}}R6,3855{{[ \t]+}}; encoding: [0x02,0x66,0x0f,0x0f]
; CHECK: CI{{[ \t]+}}R7,8738{{[ \t]+}}; encoding: [0x02,0x87,0x22,0x22]
; CHECK: CI{{[ \t]+}}R8,0{{[ \t]+}}; encoding: [0x02,0x88,0x00,0x00]

; --- Internal register ---
; CHECK: LWPI{{[ \t]+}}4096{{[ \t]+}}; encoding: [0x02,0xe0,0x10,0x00]
; CHECK: LIMI{{[ \t]+}}4{{[ \t]+}}; encoding: [0x03,0x00,0x00,0x04]
; CHECK: STST{{[ \t]+}}R1{{[ \t]+}}; encoding: [0x02,0xc1]
; CHECK: STWP{{[ \t]+}}R2{{[ \t]+}}; encoding: [0x02,0xa2]

; --- No-operand ---
; CHECK: RTWP{{[ \t]+}}; encoding: [0x03,0x80]
; CHECK: IDLE{{[ \t]+}}; encoding: [0x03,0x40]
; CHECK: RSET{{[ \t]+}}; encoding: [0x03,0x60]
; CHECK: CKOF{{[ \t]+}}; encoding: [0x03,0xc0]
; CHECK: CKON{{[ \t]+}}; encoding: [0x03,0xa0]
; CHECK: LREX{{[ \t]+}}; encoding: [0x03,0xe0]


; ===========================================================================
; DISASM lines - Disassembly round-trip verification
; ===========================================================================

; --- MOV addressing modes ---
; DISASM: c0 81{{[ \t]+}}MOV{{[ \t]+}}R1,R2
; DISASM: c1 13{{[ \t]+}}MOV{{[ \t]+}}*R3,R4
; DISASM: c0 a1 00 04{{[ \t]+}}MOV{{[ \t]+}}@4(R1),R2
; DISASM: c1 e0 12 34{{[ \t]+}}MOV{{[ \t]+}}@0x1234,R7
; DISASM: c1 b5{{[ \t]+}}MOV{{[ \t]+}}*R5+,R6
; DISASM: c4 81{{[ \t]+}}MOV{{[ \t]+}}R1,*R2
; DISASM: c9 85 00 10{{[ \t]+}}MOV{{[ \t]+}}R5,@16(R6)
; DISASM: c8 02 12 34{{[ \t]+}}MOV{{[ \t]+}}R2,@0x1234
; DISASM: ce 89{{[ \t]+}}MOV{{[ \t]+}}R9,*R10+
; DISASM: c4 91{{[ \t]+}}MOV{{[ \t]+}}*R1,*R2
; DISASM: c4 e0 11 11{{[ \t]+}}MOV{{[ \t]+}}@0x1111,*R3
; DISASM: c5 64 00 06{{[ \t]+}}MOV{{[ \t]+}}@6(R4),*R5
; DISASM: c5 f6{{[ \t]+}}MOV{{[ \t]+}}*R6+,*R7
; DISASM: c8 11 22 22{{[ \t]+}}MOV{{[ \t]+}}*R1,@0x2222
; DISASM: c8 d2 00 08{{[ \t]+}}MOV{{[ \t]+}}*R2,@8(R3)
; DISASM: cd 54{{[ \t]+}}MOV{{[ \t]+}}*R4,*R5+
; DISASM: c8 20 11 11 22 22{{[ \t]+}}MOV{{[ \t]+}}@0x1111,@0x2222
; DISASM: c8 60 12 34 00 08{{[ \t]+}}MOV{{[ \t]+}}@0x1234,@8(R1)
; DISASM: c8 22 00 06 34 56{{[ \t]+}}MOV{{[ \t]+}}@6(R2),@0x3456
; DISASM: c9 23 00 02 00 04{{[ \t]+}}MOV{{[ \t]+}}@2(R3),@4(R4)
; DISASM: c8 31 33 33{{[ \t]+}}MOV{{[ \t]+}}*R1+,@0x3333
; DISASM: c8 f2 00 0a{{[ \t]+}}MOV{{[ \t]+}}*R2+,@10(R3)
; DISASM: cd 20 44 44{{[ \t]+}}MOV{{[ \t]+}}@0x4444,*R4+
; DISASM: cd a5 00 0c{{[ \t]+}}MOV{{[ \t]+}}@12(R5),*R6+
; DISASM: ce 37{{[ \t]+}}MOV{{[ \t]+}}*R7+,*R8+

; --- MOVB ---
; DISASM: d0 81{{[ \t]+}}MOVB{{[ \t]+}}R1,R2
; DISASM: d1 13{{[ \t]+}}MOVB{{[ \t]+}}*R3,R4
; DISASM: d1 60 12 34{{[ \t]+}}MOVB{{[ \t]+}}@0x1234,R5
; DISASM: d1 e6 00 04{{[ \t]+}}MOVB{{[ \t]+}}@4(R6),R7
; DISASM: d2 78{{[ \t]+}}MOVB{{[ \t]+}}*R8+,R9
; DISASM: d6 ca{{[ \t]+}}MOVB{{[ \t]+}}R10,*R11
; DISASM: d8 0c 56 78{{[ \t]+}}MOVB{{[ \t]+}}R12,@0x5678
; DISASM: db 8d 00 06{{[ \t]+}}MOVB{{[ \t]+}}R13,@6(R14)
; DISASM: dc 4f{{[ \t]+}}MOVB{{[ \t]+}}R15,*R1+
; DISASM: d4 d2{{[ \t]+}}MOVB{{[ \t]+}}*R2,*R3
; DISASM: d8 20 11 11 22 22{{[ \t]+}}MOVB{{[ \t]+}}@0x1111,@0x2222

; --- A ---
; DISASM: a0 81{{[ \t]+}}A{{[ \t]+}}R1,R2
; DISASM: a1 13{{[ \t]+}}A{{[ \t]+}}*R3,R4
; DISASM: a1 60 12 34{{[ \t]+}}A{{[ \t]+}}@0x1234,R5
; DISASM: a1 e6 00 04{{[ \t]+}}A{{[ \t]+}}@4(R6),R7
; DISASM: a2 78{{[ \t]+}}A{{[ \t]+}}*R8+,R9
; DISASM: a4 81{{[ \t]+}}A{{[ \t]+}}R1,*R2
; DISASM: a8 03 12 34{{[ \t]+}}A{{[ \t]+}}R3,@0x1234
; DISASM: a9 44 00 06{{[ \t]+}}A{{[ \t]+}}R4,@6(R5)
; DISASM: ad c6{{[ \t]+}}A{{[ \t]+}}R6,*R7+
; DISASM: a8 20 11 11 22 22{{[ \t]+}}A{{[ \t]+}}@0x1111,@0x2222

; --- AB ---
; DISASM: b0 81{{[ \t]+}}AB{{[ \t]+}}R1,R2
; DISASM: b1 13{{[ \t]+}}AB{{[ \t]+}}*R3,R4
; DISASM: b1 60 12 34{{[ \t]+}}AB{{[ \t]+}}@0x1234,R5
; DISASM: b1 e6 00 04{{[ \t]+}}AB{{[ \t]+}}@4(R6),R7
; DISASM: b2 78{{[ \t]+}}AB{{[ \t]+}}*R8+,R9
; DISASM: b4 81{{[ \t]+}}AB{{[ \t]+}}R1,*R2

; --- S ---
; DISASM: 60 81{{[ \t]+}}S{{[ \t]+}}R1,R2
; DISASM: 61 13{{[ \t]+}}S{{[ \t]+}}*R3,R4
; DISASM: 61 60 12 34{{[ \t]+}}S{{[ \t]+}}@0x1234,R5
; DISASM: 61 e6 00 04{{[ \t]+}}S{{[ \t]+}}@4(R6),R7
; DISASM: 62 78{{[ \t]+}}S{{[ \t]+}}*R8+,R9
; DISASM: 64 81{{[ \t]+}}S{{[ \t]+}}R1,*R2
; DISASM: 68 03 23 45{{[ \t]+}}S{{[ \t]+}}R3,@0x2345
; DISASM: 69 44 00 08{{[ \t]+}}S{{[ \t]+}}R4,@8(R5)

; --- SB ---
; DISASM: 70 81{{[ \t]+}}SB{{[ \t]+}}R1,R2
; DISASM: 71 13{{[ \t]+}}SB{{[ \t]+}}*R3,R4
; DISASM: 75 85{{[ \t]+}}SB{{[ \t]+}}R5,*R6

; --- C ---
; DISASM: 81 46{{[ \t]+}}C{{[ \t]+}}R5,R6
; DISASM: 80 91{{[ \t]+}}C{{[ \t]+}}*R1,R2
; DISASM: 80 e0 12 34{{[ \t]+}}C{{[ \t]+}}@0x1234,R3
; DISASM: 81 a5 00 04{{[ \t]+}}C{{[ \t]+}}@4(R5),R6
; DISASM: 82 37{{[ \t]+}}C{{[ \t]+}}*R7+,R8
; DISASM: 84 81{{[ \t]+}}C{{[ \t]+}}R1,*R2
; DISASM: 88 03 12 34{{[ \t]+}}C{{[ \t]+}}R3,@0x1234
; DISASM: 89 44 00 06{{[ \t]+}}C{{[ \t]+}}R4,@6(R5)
; DISASM: 8d c6{{[ \t]+}}C{{[ \t]+}}R6,*R7+
; DISASM: 88 20 11 11 22 22{{[ \t]+}}C{{[ \t]+}}@0x1111,@0x2222

; --- CB ---
; DISASM: 90 42{{[ \t]+}}CB{{[ \t]+}}R1,R2
; DISASM: 91 13{{[ \t]+}}CB{{[ \t]+}}*R3,R4
; DISASM: 91 60 12 34{{[ \t]+}}CB{{[ \t]+}}@0x1234,R5
; DISASM: 95 c6{{[ \t]+}}CB{{[ \t]+}}R6,*R7

; --- SOC ---
; DISASM: e0 81{{[ \t]+}}SOC{{[ \t]+}}R1,R2
; DISASM: e1 13{{[ \t]+}}SOC{{[ \t]+}}*R3,R4
; DISASM: e1 60 12 34{{[ \t]+}}SOC{{[ \t]+}}@0x1234,R5
; DISASM: e1 e6 00 04{{[ \t]+}}SOC{{[ \t]+}}@4(R6),R7
; DISASM: e2 78{{[ \t]+}}SOC{{[ \t]+}}*R8+,R9
; DISASM: e4 81{{[ \t]+}}SOC{{[ \t]+}}R1,*R2
; DISASM: e8 03 23 45{{[ \t]+}}SOC{{[ \t]+}}R3,@0x2345
; DISASM: ed 44{{[ \t]+}}SOC{{[ \t]+}}R4,*R5+

; --- SOCB ---
; DISASM: f0 81{{[ \t]+}}SOCB{{[ \t]+}}R1,R2
; DISASM: f1 13{{[ \t]+}}SOCB{{[ \t]+}}*R3,R4
; DISASM: f5 85{{[ \t]+}}SOCB{{[ \t]+}}R5,*R6

; --- SZC ---
; DISASM: 40 81{{[ \t]+}}SZC{{[ \t]+}}R1,R2
; DISASM: 41 13{{[ \t]+}}SZC{{[ \t]+}}*R3,R4
; DISASM: 41 60 12 34{{[ \t]+}}SZC{{[ \t]+}}@0x1234,R5
; DISASM: 41 e6 00 04{{[ \t]+}}SZC{{[ \t]+}}@4(R6),R7
; DISASM: 42 78{{[ \t]+}}SZC{{[ \t]+}}*R8+,R9
; DISASM: 44 81{{[ \t]+}}SZC{{[ \t]+}}R1,*R2
; DISASM: 48 03 23 45{{[ \t]+}}SZC{{[ \t]+}}R3,@0x2345
; DISASM: 4d 44{{[ \t]+}}SZC{{[ \t]+}}R4,*R5+

; --- SZCB ---
; DISASM: 50 81{{[ \t]+}}SZCB{{[ \t]+}}R1,R2
; DISASM: 51 13{{[ \t]+}}SZCB{{[ \t]+}}*R3,R4
; DISASM: 55 85{{[ \t]+}}SZCB{{[ \t]+}}R5,*R6

; --- Format 2: COC ---
; DISASM: 21 03{{[ \t]+}}COC{{[ \t]+}}R3,R4
; DISASM: 20 91{{[ \t]+}}COC{{[ \t]+}}*R1,R2
; DISASM: 20 e0 12 34{{[ \t]+}}COC{{[ \t]+}}@0x1234,R3
; DISASM: 21 64 00 04{{[ \t]+}}COC{{[ \t]+}}@4(R4),R5
; DISASM: 21 f6{{[ \t]+}}COC{{[ \t]+}}*R6+,R7

; --- CZC ---
; DISASM: 25 85{{[ \t]+}}CZC{{[ \t]+}}R5,R6
; DISASM: 24 91{{[ \t]+}}CZC{{[ \t]+}}*R1,R2
; DISASM: 24 e0 12 34{{[ \t]+}}CZC{{[ \t]+}}@0x1234,R3
; DISASM: 25 64 00 04{{[ \t]+}}CZC{{[ \t]+}}@4(R4),R5
; DISASM: 25 f6{{[ \t]+}}CZC{{[ \t]+}}*R6+,R7

; --- XOR ---
; DISASM: 29 03{{[ \t]+}}XOR{{[ \t]+}}R3,R4
; DISASM: 29 95{{[ \t]+}}XOR{{[ \t]+}}*R5,R6
; DISASM: 29 e0 12 34{{[ \t]+}}XOR{{[ \t]+}}@0x1234,R7
; DISASM: 2a 68 00 04{{[ \t]+}}XOR{{[ \t]+}}@4(R8),R9
; DISASM: 2a fa{{[ \t]+}}XOR{{[ \t]+}}*R10+,R11

; --- MPY ---
; DISASM: 38 03{{[ \t]+}}MPY{{[ \t]+}}R3,R0
; DISASM: 39 05{{[ \t]+}}MPY{{[ \t]+}}R5,R4
; DISASM: 3a 01{{[ \t]+}}MPY{{[ \t]+}}R1,R8
; DISASM: 39 91{{[ \t]+}}MPY{{[ \t]+}}*R1,R6
; DISASM: 38 a0 01 00{{[ \t]+}}MPY{{[ \t]+}}@0x0100,R2
; DISASM: 39 23 00 04{{[ \t]+}}MPY{{[ \t]+}}@4(R3),R4
; DISASM: 39 b5{{[ \t]+}}MPY{{[ \t]+}}*R5+,R6

; --- DIV ---
; DISASM: 3c 02{{[ \t]+}}DIV{{[ \t]+}}R2,R0
; DISASM: 3d 03{{[ \t]+}}DIV{{[ \t]+}}R3,R4
; DISASM: 3c 95{{[ \t]+}}DIV{{[ \t]+}}*R5,R2
; DISASM: 3c 20 02 00{{[ \t]+}}DIV{{[ \t]+}}@0x0200,R0
; DISASM: 3d 29 00 08{{[ \t]+}}DIV{{[ \t]+}}@8(R9),R4
; DISASM: 3e 36{{[ \t]+}}DIV{{[ \t]+}}*R6+,R8

; --- Format 3: CLR ---
; DISASM: 04 c1{{[ \t]+}}CLR{{[ \t]+}}R1
; DISASM: 04 d2{{[ \t]+}}CLR{{[ \t]+}}*R2
; DISASM: 04 f3{{[ \t]+}}CLR{{[ \t]+}}*R3+
; DISASM: 04 e0 12 34{{[ \t]+}}CLR{{[ \t]+}}@0x1234
; DISASM: 04 e5 00 04{{[ \t]+}}CLR{{[ \t]+}}@4(R5)

; --- SETO ---
; DISASM: 07 01{{[ \t]+}}SETO{{[ \t]+}}R1
; DISASM: 07 12{{[ \t]+}}SETO{{[ \t]+}}*R2
; DISASM: 07 33{{[ \t]+}}SETO{{[ \t]+}}*R3+
; DISASM: 07 20 12 34{{[ \t]+}}SETO{{[ \t]+}}@0x1234
; DISASM: 07 25 00 04{{[ \t]+}}SETO{{[ \t]+}}@4(R5)

; --- NEG ---
; DISASM: 05 01{{[ \t]+}}NEG{{[ \t]+}}R1
; DISASM: 05 12{{[ \t]+}}NEG{{[ \t]+}}*R2
; DISASM: 05 33{{[ \t]+}}NEG{{[ \t]+}}*R3+
; DISASM: 05 20 12 34{{[ \t]+}}NEG{{[ \t]+}}@0x1234
; DISASM: 05 25 00 04{{[ \t]+}}NEG{{[ \t]+}}@4(R5)

; --- INV ---
; DISASM: 05 41{{[ \t]+}}INV{{[ \t]+}}R1
; DISASM: 05 52{{[ \t]+}}INV{{[ \t]+}}*R2
; DISASM: 05 73{{[ \t]+}}INV{{[ \t]+}}*R3+
; DISASM: 05 60 12 34{{[ \t]+}}INV{{[ \t]+}}@0x1234
; DISASM: 05 65 00 04{{[ \t]+}}INV{{[ \t]+}}@4(R5)

; --- INC ---
; DISASM: 05 81{{[ \t]+}}INC{{[ \t]+}}R1
; DISASM: 05 92{{[ \t]+}}INC{{[ \t]+}}*R2
; DISASM: 05 b3{{[ \t]+}}INC{{[ \t]+}}*R3+
; DISASM: 05 a0 12 34{{[ \t]+}}INC{{[ \t]+}}@0x1234
; DISASM: 05 a5 00 04{{[ \t]+}}INC{{[ \t]+}}@4(R5)

; --- INCT ---
; DISASM: 05 c1{{[ \t]+}}INCT{{[ \t]+}}R1
; DISASM: 05 d2{{[ \t]+}}INCT{{[ \t]+}}*R2
; DISASM: 05 f3{{[ \t]+}}INCT{{[ \t]+}}*R3+
; DISASM: 05 e0 12 34{{[ \t]+}}INCT{{[ \t]+}}@0x1234
; DISASM: 05 e5 00 04{{[ \t]+}}INCT{{[ \t]+}}@4(R5)

; --- DEC ---
; DISASM: 06 01{{[ \t]+}}DEC{{[ \t]+}}R1
; DISASM: 06 12{{[ \t]+}}DEC{{[ \t]+}}*R2
; DISASM: 06 33{{[ \t]+}}DEC{{[ \t]+}}*R3+
; DISASM: 06 20 12 34{{[ \t]+}}DEC{{[ \t]+}}@0x1234
; DISASM: 06 25 00 04{{[ \t]+}}DEC{{[ \t]+}}@4(R5)

; --- DECT ---
; DISASM: 06 41{{[ \t]+}}DECT{{[ \t]+}}R1
; DISASM: 06 52{{[ \t]+}}DECT{{[ \t]+}}*R2
; DISASM: 06 73{{[ \t]+}}DECT{{[ \t]+}}*R3+
; DISASM: 06 60 12 34{{[ \t]+}}DECT{{[ \t]+}}@0x1234
; DISASM: 06 65 00 04{{[ \t]+}}DECT{{[ \t]+}}@4(R5)

; --- ABS ---
; DISASM: 07 41{{[ \t]+}}ABS{{[ \t]+}}R1
; DISASM: 07 52{{[ \t]+}}ABS{{[ \t]+}}*R2
; DISASM: 07 73{{[ \t]+}}ABS{{[ \t]+}}*R3+
; DISASM: 07 60 12 34{{[ \t]+}}ABS{{[ \t]+}}@0x1234
; DISASM: 07 65 00 04{{[ \t]+}}ABS{{[ \t]+}}@4(R5)

; --- SWPB ---
; DISASM: 06 c1{{[ \t]+}}SWPB{{[ \t]+}}R1
; DISASM: 06 d2{{[ \t]+}}SWPB{{[ \t]+}}*R2
; DISASM: 06 f3{{[ \t]+}}SWPB{{[ \t]+}}*R3+
; DISASM: 06 e0 12 34{{[ \t]+}}SWPB{{[ \t]+}}@0x1234
; DISASM: 06 e5 00 04{{[ \t]+}}SWPB{{[ \t]+}}@4(R5)

; --- B ---
; DISASM: 04 4b{{[ \t]+}}B{{[ \t]+}}R11
; DISASM: 04 5b{{[ \t]+}}B{{[ \t]+}}*R11

; --- BL ---
; DISASM: 06 8d{{[ \t]+}}BL{{[ \t]+}}R13
; DISASM: 06 9d{{[ \t]+}}BL{{[ \t]+}}*R13

; --- BLWP ---
; DISASM: 04 03{{[ \t]+}}BLWP{{[ \t]+}}R3
; DISASM: 04 12{{[ \t]+}}BLWP{{[ \t]+}}*R2
; DISASM: 04 34{{[ \t]+}}BLWP{{[ \t]+}}*R4+
; DISASM: 04 20 00 40{{[ \t]+}}BLWP{{[ \t]+}}@0x0040
; DISASM: 04 25 00 08{{[ \t]+}}BLWP{{[ \t]+}}@8(R5)

; --- X ---
; DISASM: 04 88{{[ \t]+}}X{{[ \t]+}}R8
; DISASM: 04 97{{[ \t]+}}X{{[ \t]+}}*R7
; DISASM: 04 ba{{[ \t]+}}X{{[ \t]+}}*R10+
; DISASM: 04 a0 56 78{{[ \t]+}}X{{[ \t]+}}@0x5678
; DISASM: 04 a9 00 08{{[ \t]+}}X{{[ \t]+}}@8(R9)

; --- LDCR ---
; DISASM: 30 05{{[ \t]+}}LDCR{{[ \t]+}}R5,0
; DISASM: 32 01{{[ \t]+}}LDCR{{[ \t]+}}R1,8
; DISASM: 32 16{{[ \t]+}}LDCR{{[ \t]+}}*R6,8
; DISASM: 31 37{{[ \t]+}}LDCR{{[ \t]+}}*R7+,4
; DISASM: 30 a0 20 00{{[ \t]+}}LDCR{{[ \t]+}}@0x2000,2
; DISASM: 30 68 00 06{{[ \t]+}}LDCR{{[ \t]+}}@6(R8),1

; --- STCR ---
; DISASM: 34 09{{[ \t]+}}STCR{{[ \t]+}}R9,0
; DISASM: 35 c1{{[ \t]+}}STCR{{[ \t]+}}R1,7
; DISASM: 35 da{{[ \t]+}}STCR{{[ \t]+}}*R10,7
; DISASM: 35 3b{{[ \t]+}}STCR{{[ \t]+}}*R11+,4
; DISASM: 34 e0 30 00{{[ \t]+}}STCR{{[ \t]+}}@0x3000,3
; DISASM: 34 ac 00 0a{{[ \t]+}}STCR{{[ \t]+}}@10(R12),2

; --- XOP ---
; DISASM: 2c c1{{[ \t]+}}XOP{{[ \t]+}}R1,3
; DISASM: 2d 12{{[ \t]+}}XOP{{[ \t]+}}*R2,4
; DISASM: 2d 73{{[ \t]+}}XOP{{[ \t]+}}*R3+,5
; DISASM: 2d a0 12 34{{[ \t]+}}XOP{{[ \t]+}}@0x1234,6
; DISASM: 2d e4 00 08{{[ \t]+}}XOP{{[ \t]+}}@8(R4),7

; --- SBO, SBZ, TB ---
; DISASM: 1d 05{{[ \t]+}}SBO{{[ \t]+}}5
; DISASM: 1d 00{{[ \t]+}}SBO{{[ \t]+}}0
; DISASM: 1d ff{{[ \t]+}}SBO{{[ \t]+}}-1
; DISASM: 1e ff{{[ \t]+}}SBZ{{[ \t]+}}-1
; DISASM: 1e 00{{[ \t]+}}SBZ{{[ \t]+}}0
; DISASM: 1e 7f{{[ \t]+}}SBZ{{[ \t]+}}127
; DISASM: 1f 7f{{[ \t]+}}TB{{[ \t]+}}127
; DISASM: 1f 00{{[ \t]+}}TB{{[ \t]+}}0
; DISASM: 1f 80{{[ \t]+}}TB{{[ \t]+}}-128

; --- Jumps (disassembled as absolute addresses) ---
; DISASM: 10 00{{[ \t]+}}NOP
; DISASM: 13{{[ \t]}}{{.*}}JEQ
; DISASM: 16{{[ \t]}}{{.*}}JNE
; DISASM: 15{{[ \t]}}{{.*}}JGT
; DISASM: 11{{[ \t]}}{{.*}}JLT
; DISASM: 1b{{[ \t]}}{{.*}}JH
; DISASM: 14{{[ \t]}}{{.*}}JHE
; DISASM: 1a{{[ \t]}}{{.*}}JL
; DISASM: 12{{[ \t]}}{{.*}}JLE
; DISASM: 18{{[ \t]}}{{.*}}JOC
; DISASM: 17{{[ \t]}}{{.*}}JNC
; DISASM: 19{{[ \t]}}{{.*}}JNO
; DISASM: 1c{{[ \t]}}{{.*}}JOP
; DISASM: 10{{[ \t]}}{{.*}}JMP
; DISASM: 10{{[ \t]}}{{.*}}JMP

; --- Shifts ---
; DISASM: 0a 11{{[ \t]+}}SLA{{[ \t]+}}R1,1
; DISASM: 0a 82{{[ \t]+}}SLA{{[ \t]+}}R2,8
; DISASM: 0a f3{{[ \t]+}}SLA{{[ \t]+}}R3,15
; DISASM: 08 14{{[ \t]+}}SRA{{[ \t]+}}R4,1
; DISASM: 08 85{{[ \t]+}}SRA{{[ \t]+}}R5,8
; DISASM: 08 f6{{[ \t]+}}SRA{{[ \t]+}}R6,15
; DISASM: 09 17{{[ \t]+}}SRL{{[ \t]+}}R7,1
; DISASM: 09 88{{[ \t]+}}SRL{{[ \t]+}}R8,8
; DISASM: 09 f9{{[ \t]+}}SRL{{[ \t]+}}R9,15
; DISASM: 0b 1a{{[ \t]+}}SRC{{[ \t]+}}R10,1
; DISASM: 0b 8b{{[ \t]+}}SRC{{[ \t]+}}R11,8
; DISASM: 0b fc{{[ \t]+}}SRC{{[ \t]+}}R12,15
; DISASM: 0a 01{{[ \t]+}}SLA{{[ \t]+}}R1,0
; DISASM: 08 02{{[ \t]+}}SRA{{[ \t]+}}R2,0
; DISASM: 09 03{{[ \t]+}}SRL{{[ \t]+}}R3,0
; DISASM: 0b 04{{[ \t]+}}SRC{{[ \t]+}}R4,0

; --- Immediate ---
; DISASM: 02 01 12 34{{[ \t]+}}LI{{[ \t]+}}R1,4660
; DISASM: 02 05 00 00{{[ \t]+}}LI{{[ \t]+}}R5,0
; DISASM: 02 0f ff ff{{[ \t]+}}LI{{[ \t]+}}R15,65535
; DISASM: 02 22 01 00{{[ \t]+}}AI{{[ \t]+}}R2,256
; DISASM: 02 23 ff ff{{[ \t]+}}AI{{[ \t]+}}R3,65535
; DISASM: 02 44 00 ff{{[ \t]+}}ANDI{{[ \t]+}}R4,255
; DISASM: 02 45 ff 00{{[ \t]+}}ANDI{{[ \t]+}}R5,65280
; DISASM: 02 66 0f 0f{{[ \t]+}}ORI{{[ \t]+}}R6,3855
; DISASM: 02 87 22 22{{[ \t]+}}CI{{[ \t]+}}R7,8738
; DISASM: 02 88 00 00{{[ \t]+}}CI{{[ \t]+}}R8,0

; --- Internal register ---
; DISASM: 02 e0 10 00{{[ \t]+}}LWPI{{[ \t]+}}4096
; DISASM: 03 00 00 04{{[ \t]+}}LIMI{{[ \t]+}}4
; DISASM: 02 c1{{[ \t]+}}STST{{[ \t]+}}R1
; DISASM: 02 a2{{[ \t]+}}STWP{{[ \t]+}}R2

; --- No-operand ---
; DISASM: 03 80{{[ \t]+}}RTWP
; DISASM: 03 40{{[ \t]+}}IDLE
; DISASM: 03 60{{[ \t]+}}RSET
; DISASM: 03 c0{{[ \t]+}}CKOF
; DISASM: 03 a0{{[ \t]+}}CKON
; DISASM: 03 e0{{[ \t]+}}LREX
