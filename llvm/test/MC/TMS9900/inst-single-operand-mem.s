; RUN: llvm-mc -triple tms9900 -show-encoding < %s | FileCheck %s
; RUN: llvm-mc -filetype=obj -triple tms9900 < %s \
; RUN:   | llvm-objdump -d - | FileCheck --check-prefix=DISASM %s

  clr *r1
  clr *r2+
  clr @0x1234
  clr @4(r5)
  neg *r3
  neg *r4+
  neg @0x2345
  neg @6(r6)
  inv *r7
  inv *r8+
  inv @0x3456
  inv @8(r9)
  inc *r10
  inc *r11+
  inc @0x4567
  inc @10(r12)
  inct *r13
  inct *r14+
  inct @0x5678
  inct @12(r15)
  dec *r1
  dec *r2+
  dec @0x6789
  dec @14(r3)
  dect *r4
  dect *r5+
  dect @0x789a
  dect @16(r6)
  seto *r7
  seto *r8+
  seto @0x1111
  seto @18(r9)
  abs *r10
  abs *r11+
  abs @0x2222
  abs @20(r12)
  swpb *r13
  swpb *r14+
  swpb @0x3333
  swpb @22(r15)

; CHECK: CLR{{[ \t]+}}*R1{{[ \t]+}}; encoding: [0x04,0xd1]
; CHECK: CLR{{[ \t]+}}*R2+{{[ \t]+}}; encoding: [0x04,0xf2]
; CHECK: CLR{{[ \t]+}}@0x1234{{[ \t]+}}; encoding: [0x04,0xe0,0x12,0x34]
; CHECK: CLR{{[ \t]+}}@4(R5){{[ \t]+}}; encoding: [0x04,0xe5,0x00,0x04]
; CHECK: NEG{{[ \t]+}}*R3{{[ \t]+}}; encoding: [0x05,0x13]
; CHECK: NEG{{[ \t]+}}*R4+{{[ \t]+}}; encoding: [0x05,0x34]
; CHECK: NEG{{[ \t]+}}@0x2345{{[ \t]+}}; encoding: [0x05,0x20,0x23,0x45]
; CHECK: NEG{{[ \t]+}}@6(R6){{[ \t]+}}; encoding: [0x05,0x26,0x00,0x06]
; CHECK: INV{{[ \t]+}}*R7{{[ \t]+}}; encoding: [0x05,0x57]
; CHECK: INV{{[ \t]+}}*R8+{{[ \t]+}}; encoding: [0x05,0x78]
; CHECK: INV{{[ \t]+}}@0x3456{{[ \t]+}}; encoding: [0x05,0x60,0x34,0x56]
; CHECK: INV{{[ \t]+}}@8(R9){{[ \t]+}}; encoding: [0x05,0x69,0x00,0x08]
; CHECK: INC{{[ \t]+}}*R10{{[ \t]+}}; encoding: [0x05,0x9a]
; CHECK: INC{{[ \t]+}}*R11+{{[ \t]+}}; encoding: [0x05,0xbb]
; CHECK: INC{{[ \t]+}}@0x4567{{[ \t]+}}; encoding: [0x05,0xa0,0x45,0x67]
; CHECK: INC{{[ \t]+}}@10(R12){{[ \t]+}}; encoding: [0x05,0xac,0x00,0x0a]
; CHECK: INCT{{[ \t]+}}*R13{{[ \t]+}}; encoding: [0x05,0xdd]
; CHECK: INCT{{[ \t]+}}*R14+{{[ \t]+}}; encoding: [0x05,0xfe]
; CHECK: INCT{{[ \t]+}}@0x5678{{[ \t]+}}; encoding: [0x05,0xe0,0x56,0x78]
; CHECK: INCT{{[ \t]+}}@12(R15){{[ \t]+}}; encoding: [0x05,0xef,0x00,0x0c]
; CHECK: DEC{{[ \t]+}}*R1{{[ \t]+}}; encoding: [0x06,0x11]
; CHECK: DEC{{[ \t]+}}*R2+{{[ \t]+}}; encoding: [0x06,0x32]
; CHECK: DEC{{[ \t]+}}@0x6789{{[ \t]+}}; encoding: [0x06,0x20,0x67,0x89]
; CHECK: DEC{{[ \t]+}}@14(R3){{[ \t]+}}; encoding: [0x06,0x23,0x00,0x0e]
; CHECK: DECT{{[ \t]+}}*R4{{[ \t]+}}; encoding: [0x06,0x54]
; CHECK: DECT{{[ \t]+}}*R5+{{[ \t]+}}; encoding: [0x06,0x75]
; CHECK: DECT{{[ \t]+}}@0x789a{{[ \t]+}}; encoding: [0x06,0x60,0x78,0x9a]
; CHECK: DECT{{[ \t]+}}@16(R6){{[ \t]+}}; encoding: [0x06,0x66,0x00,0x10]
; CHECK: SETO{{[ \t]+}}*R7{{[ \t]+}}; encoding: [0x07,0x17]
; CHECK: SETO{{[ \t]+}}*R8+{{[ \t]+}}; encoding: [0x07,0x38]
; CHECK: SETO{{[ \t]+}}@0x1111{{[ \t]+}}; encoding: [0x07,0x20,0x11,0x11]
; CHECK: SETO{{[ \t]+}}@18(R9){{[ \t]+}}; encoding: [0x07,0x29,0x00,0x12]
; CHECK: ABS{{[ \t]+}}*R10{{[ \t]+}}; encoding: [0x07,0x5a]
; CHECK: ABS{{[ \t]+}}*R11+{{[ \t]+}}; encoding: [0x07,0x7b]
; CHECK: ABS{{[ \t]+}}@0x2222{{[ \t]+}}; encoding: [0x07,0x60,0x22,0x22]
; CHECK: ABS{{[ \t]+}}@20(R12){{[ \t]+}}; encoding: [0x07,0x6c,0x00,0x14]
; CHECK: SWPB{{[ \t]+}}*R13{{[ \t]+}}; encoding: [0x06,0xdd]
; CHECK: SWPB{{[ \t]+}}*R14+{{[ \t]+}}; encoding: [0x06,0xfe]
; CHECK: SWPB{{[ \t]+}}@0x3333{{[ \t]+}}; encoding: [0x06,0xe0,0x33,0x33]
; CHECK: SWPB{{[ \t]+}}@22(R15){{[ \t]+}}; encoding: [0x06,0xef,0x00,0x16]

; DISASM: {{[0-9a-f]+}}: 04 d1{{[ \t]+}}CLR{{[ \t]+}}*R1
; DISASM: {{[0-9a-f]+}}: 04 f2{{[ \t]+}}CLR{{[ \t]+}}*R2+
; DISASM: {{[0-9a-f]+}}: 04 e0 12 34{{[ \t]+}}CLR{{[ \t]+}}@0x1234
; DISASM: {{[0-9a-f]+}}: 04 e5 00 04{{[ \t]+}}CLR{{[ \t]+}}@4(R5)
; DISASM: {{[0-9a-f]+}}: 05 13{{[ \t]+}}NEG{{[ \t]+}}*R3
; DISASM: {{[0-9a-f]+}}: 05 34{{[ \t]+}}NEG{{[ \t]+}}*R4+
; DISASM: {{[0-9a-f]+}}: 05 20 23 45{{[ \t]+}}NEG{{[ \t]+}}@0x2345
; DISASM: {{[0-9a-f]+}}: 05 26 00 06{{[ \t]+}}NEG{{[ \t]+}}@6(R6)
; DISASM: {{[0-9a-f]+}}: 05 57{{[ \t]+}}INV{{[ \t]+}}*R7
; DISASM: {{[0-9a-f]+}}: 05 78{{[ \t]+}}INV{{[ \t]+}}*R8+
; DISASM: {{[0-9a-f]+}}: 05 60 34 56{{[ \t]+}}INV{{[ \t]+}}@0x3456
; DISASM: {{[0-9a-f]+}}: 05 69 00 08{{[ \t]+}}INV{{[ \t]+}}@8(R9)
; DISASM: {{[0-9a-f]+}}: 05 9a{{[ \t]+}}INC{{[ \t]+}}*R10
; DISASM: {{[0-9a-f]+}}: 05 bb{{[ \t]+}}INC{{[ \t]+}}*R11+
; DISASM: {{[0-9a-f]+}}: 05 a0 45 67{{[ \t]+}}INC{{[ \t]+}}@0x4567
; DISASM: {{[0-9a-f]+}}: 05 ac 00 0a{{[ \t]+}}INC{{[ \t]+}}@10(R12)
; DISASM: {{[0-9a-f]+}}: 05 dd{{[ \t]+}}INCT{{[ \t]+}}*R13
; DISASM: {{[0-9a-f]+}}: 05 fe{{[ \t]+}}INCT{{[ \t]+}}*R14+
; DISASM: {{[0-9a-f]+}}: 05 e0 56 78{{[ \t]+}}INCT{{[ \t]+}}@0x5678
; DISASM: {{[0-9a-f]+}}: 05 ef 00 0c{{[ \t]+}}INCT{{[ \t]+}}@12(R15)
; DISASM: {{[0-9a-f]+}}: 06 11{{[ \t]+}}DEC{{[ \t]+}}*R1
; DISASM: {{[0-9a-f]+}}: 06 32{{[ \t]+}}DEC{{[ \t]+}}*R2+
; DISASM: {{[0-9a-f]+}}: 06 20 67 89{{[ \t]+}}DEC{{[ \t]+}}@0x6789
; DISASM: {{[0-9a-f]+}}: 06 23 00 0e{{[ \t]+}}DEC{{[ \t]+}}@14(R3)
; DISASM: {{[0-9a-f]+}}: 06 54{{[ \t]+}}DECT{{[ \t]+}}*R4
; DISASM: {{[0-9a-f]+}}: 06 75{{[ \t]+}}DECT{{[ \t]+}}*R5+
; DISASM: {{[0-9a-f]+}}: 06 60 78 9a{{[ \t]+}}DECT{{[ \t]+}}@0x789a
; DISASM: {{[0-9a-f]+}}: 06 66 00 10{{[ \t]+}}DECT{{[ \t]+}}@16(R6)
; DISASM: {{[0-9a-f]+}}: 07 17{{[ \t]+}}SETO{{[ \t]+}}*R7
; DISASM: {{[0-9a-f]+}}: 07 38{{[ \t]+}}SETO{{[ \t]+}}*R8+
; DISASM: {{[0-9a-f]+}}: 07 20 11 11{{[ \t]+}}SETO{{[ \t]+}}@0x1111
; DISASM: {{[0-9a-f]+}}: 07 29 00 12{{[ \t]+}}SETO{{[ \t]+}}@18(R9)
; DISASM: {{[0-9a-f]+}}: 07 5a{{[ \t]+}}ABS{{[ \t]+}}*R10
; DISASM: {{[0-9a-f]+}}: 07 7b{{[ \t]+}}ABS{{[ \t]+}}*R11+
; DISASM: {{[0-9a-f]+}}: 07 60 22 22{{[ \t]+}}ABS{{[ \t]+}}@0x2222
; DISASM: {{[0-9a-f]+}}: 07 6c 00 14{{[ \t]+}}ABS{{[ \t]+}}@20(R12)
; DISASM: {{[0-9a-f]+}}: 06 dd{{[ \t]+}}SWPB{{[ \t]+}}*R13
; DISASM: {{[0-9a-f]+}}: 06 fe{{[ \t]+}}SWPB{{[ \t]+}}*R14+
; DISASM: {{[0-9a-f]+}}: 06 e0 33 33{{[ \t]+}}SWPB{{[ \t]+}}@0x3333
; DISASM: {{[0-9a-f]+}}: 06 ef 00 16{{[ \t]+}}SWPB{{[ \t]+}}@22(R15)
