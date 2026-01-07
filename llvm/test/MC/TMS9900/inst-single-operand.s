; RUN: llvm-mc -triple tms9900 -show-encoding < %s | FileCheck %s
; RUN: llvm-mc -filetype=obj -triple tms9900 < %s \
; RUN:   | llvm-objdump -d - | FileCheck --check-prefix=DISASM %s

  clr r1
  neg r2
  inv r3
  inc r4
  inct r5
  dec r6
  dect r7
  seto r8
  abs r9
  swpb r10
  b *r11
  b r11
  bl *r13
  bl r13
  blwp *r1
  blwp r2
  blwp @0x1234
  blwp @4(r5)
  blwp *r6+
  x *r7
  x r8
  x @0x5678
  x @8(r9)
  x *r10+

; CHECK: CLR{{[ \t]+}}R1{{[ \t]+}}; encoding: [0x04,0xc1]
; CHECK: NEG{{[ \t]+}}R2{{[ \t]+}}; encoding: [0x05,0x02]
; CHECK: INV{{[ \t]+}}R3{{[ \t]+}}; encoding: [0x05,0x43]
; CHECK: INC{{[ \t]+}}R4{{[ \t]+}}; encoding: [0x05,0x84]
; CHECK: INCT{{[ \t]+}}R5{{[ \t]+}}; encoding: [0x05,0xc5]
; CHECK: DEC{{[ \t]+}}R6{{[ \t]+}}; encoding: [0x06,0x06]
; CHECK: DECT{{[ \t]+}}R7{{[ \t]+}}; encoding: [0x06,0x47]
; CHECK: SETO{{[ \t]+}}R8{{[ \t]+}}; encoding: [0x07,0x08]
; CHECK: ABS{{[ \t]+}}R9{{[ \t]+}}; encoding: [0x07,0x49]
; CHECK: SWPB{{[ \t]+}}R10{{[ \t]+}}; encoding: [0x06,0xca]
; CHECK: B{{[ \t]+}}*R11{{[ \t]+}}; encoding: [0x04,0x5b]
; CHECK: B{{[ \t]+}}R11{{[ \t]+}}; encoding: [0x04,0x4b]
; CHECK: BL{{[ \t]+}}*R13{{[ \t]+}}; encoding: [0x06,0x9d]
; CHECK: BL{{[ \t]+}}R13{{[ \t]+}}; encoding: [0x06,0x8d]
; CHECK: BLWP{{[ \t]+}}*R1{{[ \t]+}}; encoding: [0x04,0x11]
; CHECK: BLWP{{[ \t]+}}R2{{[ \t]+}}; encoding: [0x04,0x02]
; CHECK: BLWP{{[ \t]+}}@0x1234{{[ \t]+}}; encoding: [0x04,0x20,0x12,0x34]
; CHECK: BLWP{{[ \t]+}}@4(R5){{[ \t]+}}; encoding: [0x04,0x25,0x00,0x04]
; CHECK: BLWP{{[ \t]+}}*R6+{{[ \t]+}}; encoding: [0x04,0x36]
; CHECK: X{{[ \t]+}}*R7{{[ \t]+}}; encoding: [0x04,0x97]
; CHECK: X{{[ \t]+}}R8{{[ \t]+}}; encoding: [0x04,0x88]
; CHECK: X{{[ \t]+}}@0x5678{{[ \t]+}}; encoding: [0x04,0xa0,0x56,0x78]
; CHECK: X{{[ \t]+}}@8(R9){{[ \t]+}}; encoding: [0x04,0xa9,0x00,0x08]
; CHECK: X{{[ \t]+}}*R10+{{[ \t]+}}; encoding: [0x04,0xba]

; DISASM: {{[0-9a-f]+}}: 04 c1{{[ \t]+}}CLR{{[ \t]+}}R1
; DISASM: {{[0-9a-f]+}}: 05 02{{[ \t]+}}NEG{{[ \t]+}}R2
; DISASM: {{[0-9a-f]+}}: 05 43{{[ \t]+}}INV{{[ \t]+}}R3
; DISASM: {{[0-9a-f]+}}: 05 84{{[ \t]+}}INC{{[ \t]+}}R4
; DISASM: {{[0-9a-f]+}}: 05 c5{{[ \t]+}}INCT{{[ \t]+}}R5
; DISASM: {{[0-9a-f]+}}: 06 06{{[ \t]+}}DEC{{[ \t]+}}R6
; DISASM: {{[0-9a-f]+}}: 06 47{{[ \t]+}}DECT{{[ \t]+}}R7
; DISASM: {{[0-9a-f]+}}: 07 08{{[ \t]+}}SETO{{[ \t]+}}R8
; DISASM: {{[0-9a-f]+}}: 07 49{{[ \t]+}}ABS{{[ \t]+}}R9
; DISASM: {{[0-9a-f]+}}: 06 ca{{[ \t]+}}SWPB{{[ \t]+}}R10
; DISASM: {{[0-9a-f]+}}: 04 5b{{[ \t]+}}B{{[ \t]+}}*R11
; DISASM: {{[0-9a-f]+}}: 04 4b{{[ \t]+}}B{{[ \t]+}}R11
; DISASM: {{[0-9a-f]+}}: 06 9d{{[ \t]+}}BL{{[ \t]+}}*R13
; DISASM: {{[0-9a-f]+}}: 06 8d{{[ \t]+}}BL{{[ \t]+}}R13
; DISASM: {{[0-9a-f]+}}: 04 11{{[ \t]+}}BLWP{{[ \t]+}}*R1
; DISASM: {{[0-9a-f]+}}: 04 02{{[ \t]+}}BLWP{{[ \t]+}}R2
; DISASM: {{[0-9a-f]+}}: 04 20 12 34{{[ \t]+}}BLWP{{[ \t]+}}@0x1234
; DISASM: {{[0-9a-f]+}}: 04 25 00 04{{[ \t]+}}BLWP{{[ \t]+}}@4(R5)
; DISASM: {{[0-9a-f]+}}: 04 36{{[ \t]+}}BLWP{{[ \t]+}}*R6+
; DISASM: {{[0-9a-f]+}}: 04 97{{[ \t]+}}X{{[ \t]+}}*R7
; DISASM: {{[0-9a-f]+}}: 04 88{{[ \t]+}}X{{[ \t]+}}R8
; DISASM: {{[0-9a-f]+}}: 04 a0 56 78{{[ \t]+}}X{{[ \t]+}}@0x5678
; DISASM: {{[0-9a-f]+}}: 04 a9 00 08{{[ \t]+}}X{{[ \t]+}}@8(R9)
; DISASM: {{[0-9a-f]+}}: 04 ba{{[ \t]+}}X{{[ \t]+}}*R10+
