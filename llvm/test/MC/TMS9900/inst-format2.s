; RUN: llvm-mc -triple tms9900 -show-encoding < %s | FileCheck %s
; RUN: llvm-mc -filetype=obj -triple tms9900 < %s \
; RUN:   | llvm-objdump -d - | FileCheck --check-prefix=DISASM %s

  coc r3, r4
  czc r5, r6
  xor r3, r4
  mpy r1, r0
  div r2, r0
  coc *r1, r2
  czc @0x1234, r3
  xor @4(r4), r5
  xor *r6+, r7
  mpy *r8, r0
  div @8(r9), r0

; CHECK: COC{{[ \t]+}}R3,R4{{[ \t]+}}; encoding: [0x21,0x03]
; CHECK: CZC{{[ \t]+}}R5,R6{{[ \t]+}}; encoding: [0x25,0x85]
; CHECK: XOR{{[ \t]+}}R3,R4{{[ \t]+}}; encoding: [0x29,0x03]
; CHECK: MPY{{[ \t]+}}R1,R0{{[ \t]+}}; encoding: [0x38,0x01]
; CHECK: DIV{{[ \t]+}}R2,R0{{[ \t]+}}; encoding: [0x3c,0x02]
; CHECK: COC{{[ \t]+}}*R1,R2{{[ \t]+}}; encoding: [0x20,0x91]
; CHECK: CZC{{[ \t]+}}@0x1234,R3{{[ \t]+}}; encoding: [0x24,0xe0,0x12,0x34]
; CHECK: XOR{{[ \t]+}}@4(R4),R5{{[ \t]+}}; encoding: [0x29,0x64,0x00,0x04]
; CHECK: XOR{{[ \t]+}}*R6+,R7{{[ \t]+}}; encoding: [0x29,0xf6]
; CHECK: MPY{{[ \t]+}}*R8,R0{{[ \t]+}}; encoding: [0x38,0x18]
; CHECK: DIV{{[ \t]+}}@8(R9),R0{{[ \t]+}}; encoding: [0x3c,0x29,0x00,0x08]

; DISASM: {{[0-9a-f]+}}: 21 03{{[ \t]+}}COC{{[ \t]+}}R3,R4
; DISASM: {{[0-9a-f]+}}: 25 85{{[ \t]+}}CZC{{[ \t]+}}R5,R6
; DISASM: {{[0-9a-f]+}}: 29 03{{[ \t]+}}XOR{{[ \t]+}}R3,R4
; DISASM: {{[0-9a-f]+}}: 38 01{{[ \t]+}}MPY{{[ \t]+}}R1,R0
; DISASM: {{[0-9a-f]+}}: 3c 02{{[ \t]+}}DIV{{[ \t]+}}R2,R0
; DISASM: {{[0-9a-f]+}}: 20 91{{[ \t]+}}COC{{[ \t]+}}*R1,R2
; DISASM: {{[0-9a-f]+}}: 24 e0 12 34{{[ \t]+}}CZC{{[ \t]+}}@0x1234,R3
; DISASM: {{[0-9a-f]+}}: 29 64 00 04{{[ \t]+}}XOR{{[ \t]+}}@4(R4),R5
; DISASM: {{[0-9a-f]+}}: 29 f6{{[ \t]+}}XOR{{[ \t]+}}*R6+,R7
; DISASM: {{[0-9a-f]+}}: 38 18{{[ \t]+}}MPY{{[ \t]+}}*R8,R0
; DISASM: {{[0-9a-f]+}}: 3c 29 00 08{{[ \t]+}}DIV{{[ \t]+}}@8(R9),R0
