; RUN: llvm-mc -triple tms9900 -show-encoding < %s | FileCheck %s
; RUN: llvm-mc -filetype=obj -triple tms9900 < %s \
; RUN:   | llvm-objdump -d - | FileCheck --check-prefix=DISASM %s

  sla r1, 3
  sra r2, 4
  srl r3, 5
  src r4, 6
  sla r5, 0
  sra r6, 0
  srl r7, 0
  src r8, 0

; CHECK: SLA{{[ \t]+}}R1,3{{[ \t]+}}; encoding: [0x0a,0x31]
; CHECK: SRA{{[ \t]+}}R2,4{{[ \t]+}}; encoding: [0x08,0x42]
; CHECK: SRL{{[ \t]+}}R3,5{{[ \t]+}}; encoding: [0x09,0x53]
; CHECK: SRC{{[ \t]+}}R4,6{{[ \t]+}}; encoding: [0x0b,0x64]
; CHECK: SLA{{[ \t]+}}R5,0{{[ \t]+}}; encoding: [0x0a,0x05]
; CHECK: SRA{{[ \t]+}}R6,0{{[ \t]+}}; encoding: [0x08,0x06]
; CHECK: SRL{{[ \t]+}}R7,0{{[ \t]+}}; encoding: [0x09,0x07]
; CHECK: SRC{{[ \t]+}}R8,0{{[ \t]+}}; encoding: [0x0b,0x08]

; DISASM: {{[0-9a-f]+}}: 0a 31{{[ \t]+}}SLA{{[ \t]+}}R1,3
; DISASM: {{[0-9a-f]+}}: 08 42{{[ \t]+}}SRA{{[ \t]+}}R2,4
; DISASM: {{[0-9a-f]+}}: 09 53{{[ \t]+}}SRL{{[ \t]+}}R3,5
; DISASM: {{[0-9a-f]+}}: 0b 64{{[ \t]+}}SRC{{[ \t]+}}R4,6
; DISASM: {{[0-9a-f]+}}: 0a 05{{[ \t]+}}SLA{{[ \t]+}}R5,0
; DISASM: {{[0-9a-f]+}}: 08 06{{[ \t]+}}SRA{{[ \t]+}}R6,0
; DISASM: {{[0-9a-f]+}}: 09 07{{[ \t]+}}SRL{{[ \t]+}}R7,0
; DISASM: {{[0-9a-f]+}}: 0b 08{{[ \t]+}}SRC{{[ \t]+}}R8,0
