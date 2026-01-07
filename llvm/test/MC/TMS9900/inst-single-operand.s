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
