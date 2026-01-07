; RUN: llvm-mc -triple tms9900 -show-encoding < %s | FileCheck %s
; RUN: llvm-mc -filetype=obj -triple tms9900 < %s \
; RUN:   | llvm-objdump -d - | FileCheck --check-prefix=DISASM %s

  xor r3, r4
  mpy r1, r0
  div r2, r0

; CHECK: XOR{{[ \t]+}}R3,R4{{[ \t]+}}; encoding: [0x29,0x03]
; CHECK: MPY{{[ \t]+}}R1,R0{{[ \t]+}}; encoding: [0x38,0x01]
; CHECK: DIV{{[ \t]+}}R2,R0{{[ \t]+}}; encoding: [0x3c,0x02]

; DISASM: {{[0-9a-f]+}}: 29 03{{[ \t]+}}XOR{{[ \t]+}}R3,R4
; DISASM: {{[0-9a-f]+}}: 38 01{{[ \t]+}}MPY{{[ \t]+}}R1,R0
; DISASM: {{[0-9a-f]+}}: 3c 02{{[ \t]+}}DIV{{[ \t]+}}R2,R0
