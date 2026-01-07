; RUN: llvm-mc -triple tms9900 -show-encoding < %s | FileCheck %s
; RUN: llvm-mc -filetype=obj -triple tms9900 < %s \
; RUN:   | llvm-objdump -d - | FileCheck --check-prefix=DISASM %s

  xop r1, 3
  xop *r2, 4
  xop *r3+, 5
  xop @0x1234, 6
  xop @8(r4), 7

  ldcr r5, 0
  ldcr *r6, 8
  ldcr *r7+, 4
  ldcr @0x2000, 2
  ldcr @6(r8), 1

  stcr r9, 0
  stcr *r10, 7
  stcr *r11+, 4
  stcr @0x3000, 3
  stcr @10(r12), 2

  sbo 5
  sbz -1
  tb 127

; CHECK: XOP{{[ \t]+}}R1,3{{[ \t]+}}; encoding: [0x2c,0xc1]
; CHECK: XOP{{[ \t]+}}*R2,4{{[ \t]+}}; encoding: [0x2d,0x12]
; CHECK: XOP{{[ \t]+}}*R3+,5{{[ \t]+}}; encoding: [0x2d,0x73]
; CHECK: XOP{{[ \t]+}}@0x1234,6{{[ \t]+}}; encoding: [0x2d,0xa0,0x12,0x34]
; CHECK: XOP{{[ \t]+}}@8(R4),7{{[ \t]+}}; encoding: [0x2d,0xe4,0x00,0x08]

; CHECK: LDCR{{[ \t]+}}R5,0{{[ \t]+}}; encoding: [0x30,0x05]
; CHECK: LDCR{{[ \t]+}}*R6,8{{[ \t]+}}; encoding: [0x32,0x16]
; CHECK: LDCR{{[ \t]+}}*R7+,4{{[ \t]+}}; encoding: [0x31,0x37]
; CHECK: LDCR{{[ \t]+}}@0x2000,2{{[ \t]+}}; encoding: [0x30,0xa0,0x20,0x00]
; CHECK: LDCR{{[ \t]+}}@6(R8),1{{[ \t]+}}; encoding: [0x30,0x68,0x00,0x06]

; CHECK: STCR{{[ \t]+}}R9,0{{[ \t]+}}; encoding: [0x34,0x09]
; CHECK: STCR{{[ \t]+}}*R10,7{{[ \t]+}}; encoding: [0x35,0xda]
; CHECK: STCR{{[ \t]+}}*R11+,4{{[ \t]+}}; encoding: [0x35,0x3b]
; CHECK: STCR{{[ \t]+}}@0x3000,3{{[ \t]+}}; encoding: [0x34,0xe0,0x30,0x00]
; CHECK: STCR{{[ \t]+}}@10(R12),2{{[ \t]+}}; encoding: [0x34,0xac,0x00,0x0a]

; CHECK: SBO{{[ \t]+}}5{{[ \t]+}}; encoding: [0x1d,0x05]
; CHECK: SBZ{{[ \t]+}}-1{{[ \t]+}}; encoding: [0x1e,0xff]
; CHECK: TB{{[ \t]+}}127{{[ \t]+}}; encoding: [0x1f,0x7f]

; DISASM: {{[0-9a-f]+}}: 2c c1{{[ \t]+}}XOP{{[ \t]+}}R1,3
; DISASM: {{[0-9a-f]+}}: 2d 12{{[ \t]+}}XOP{{[ \t]+}}*R2,4
; DISASM: {{[0-9a-f]+}}: 2d 73{{[ \t]+}}XOP{{[ \t]+}}*R3+,5
; DISASM: {{[0-9a-f]+}}: 2d a0 12 34{{[ \t]+}}XOP{{[ \t]+}}@0x1234,6
; DISASM: {{[0-9a-f]+}}: 2d e4 00 08{{[ \t]+}}XOP{{[ \t]+}}@8(R4),7

; DISASM: {{[0-9a-f]+}}: 30 05{{[ \t]+}}LDCR{{[ \t]+}}R5,0
; DISASM: {{[0-9a-f]+}}: 32 16{{[ \t]+}}LDCR{{[ \t]+}}*R6,8
; DISASM: {{[0-9a-f]+}}: 31 37{{[ \t]+}}LDCR{{[ \t]+}}*R7+,4
; DISASM: {{[0-9a-f]+}}: 30 a0 20 00{{[ \t]+}}LDCR{{[ \t]+}}@0x2000,2
; DISASM: {{[0-9a-f]+}}: 30 68 00 06{{[ \t]+}}LDCR{{[ \t]+}}@6(R8),1

; DISASM: {{[0-9a-f]+}}: 34 09{{[ \t]+}}STCR{{[ \t]+}}R9,0
; DISASM: {{[0-9a-f]+}}: 35 da{{[ \t]+}}STCR{{[ \t]+}}*R10,7
; DISASM: {{[0-9a-f]+}}: 35 3b{{[ \t]+}}STCR{{[ \t]+}}*R11+,4
; DISASM: {{[0-9a-f]+}}: 34 e0 30 00{{[ \t]+}}STCR{{[ \t]+}}@0x3000,3
; DISASM: {{[0-9a-f]+}}: 34 ac 00 0a{{[ \t]+}}STCR{{[ \t]+}}@10(R12),2

; DISASM: {{[0-9a-f]+}}: 1d 05{{[ \t]+}}SBO{{[ \t]+}}5
; DISASM: {{[0-9a-f]+}}: 1e ff{{[ \t]+}}SBZ{{[ \t]+}}-1
; DISASM: {{[0-9a-f]+}}: 1f 7f{{[ \t]+}}TB{{[ \t]+}}127
