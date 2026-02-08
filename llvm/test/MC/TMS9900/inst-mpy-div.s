; RUN: llvm-mc -triple tms9900 -show-encoding < %s | FileCheck %s
; RUN: llvm-mc -filetype=obj -triple tms9900 < %s \
; RUN:   | llvm-objdump -d - | FileCheck --check-prefix=DISASM %s
;
; Test MPY and DIV instruction encoding with various source addressing
; modes and destination register fields.
;
; The destination register field was a previously-fixed bug where
; Format2 classes hardcoded the destination to R0.

  ; MPY with register source, various destinations
  mpy r3, r0
  mpy r5, r4
  mpy r1, r8

  ; MPY with memory addressing modes
  mpy @0x0100, r2
  mpy *r1, r6
  mpy *r2+, r8

  ; DIV with register source, various destinations
  div r2, r0
  div r3, r4
  div r8, r2

  ; DIV with memory addressing modes
  div @0x0200, r0
  div *r5, r2
  div *r6+, r8
  div @8(r9), r4

; CHECK: MPY{{[ \t]+}}R3,R0{{[ \t]+}}; encoding: [0x38,0x03]
; CHECK: MPY{{[ \t]+}}R5,R4{{[ \t]+}}; encoding: [0x39,0x05]
; CHECK: MPY{{[ \t]+}}R1,R8{{[ \t]+}}; encoding: [0x3a,0x01]
; CHECK: MPY{{[ \t]+}}@0x0100,R2{{[ \t]+}}; encoding: [0x38,0xa0,0x01,0x00]
; CHECK: MPY{{[ \t]+}}*R1,R6{{[ \t]+}}; encoding: [0x39,0x91]
; CHECK: MPY{{[ \t]+}}*R2+,R8{{[ \t]+}}; encoding: [0x3a,0x32]
; CHECK: DIV{{[ \t]+}}R2,R0{{[ \t]+}}; encoding: [0x3c,0x02]
; CHECK: DIV{{[ \t]+}}R3,R4{{[ \t]+}}; encoding: [0x3d,0x03]
; CHECK: DIV{{[ \t]+}}R8,R2{{[ \t]+}}; encoding: [0x3c,0x88]
; CHECK: DIV{{[ \t]+}}@0x0200,R0{{[ \t]+}}; encoding: [0x3c,0x20,0x02,0x00]
; CHECK: DIV{{[ \t]+}}*R5,R2{{[ \t]+}}; encoding: [0x3c,0x95]
; CHECK: DIV{{[ \t]+}}*R6+,R8{{[ \t]+}}; encoding: [0x3e,0x36]
; CHECK: DIV{{[ \t]+}}@8(R9),R4{{[ \t]+}}; encoding: [0x3d,0x29,0x00,0x08]

; DISASM: {{[0-9a-f]+}}: 38 03{{[ \t]+}}MPY{{[ \t]+}}R3,R0
; DISASM: {{[0-9a-f]+}}: 39 05{{[ \t]+}}MPY{{[ \t]+}}R5,R4
; DISASM: {{[0-9a-f]+}}: 3a 01{{[ \t]+}}MPY{{[ \t]+}}R1,R8
; DISASM: {{[0-9a-f]+}}: 38 a0 01 00{{[ \t]+}}MPY{{[ \t]+}}@0x0100,R2
; DISASM: {{[0-9a-f]+}}: 39 91{{[ \t]+}}MPY{{[ \t]+}}*R1,R6
; DISASM: {{[0-9a-f]+}}: 3a 32{{[ \t]+}}MPY{{[ \t]+}}*R2+,R8
; DISASM: {{[0-9a-f]+}}: 3c 02{{[ \t]+}}DIV{{[ \t]+}}R2,R0
; DISASM: {{[0-9a-f]+}}: 3d 03{{[ \t]+}}DIV{{[ \t]+}}R3,R4
; DISASM: {{[0-9a-f]+}}: 3c 88{{[ \t]+}}DIV{{[ \t]+}}R8,R2
; DISASM: {{[0-9a-f]+}}: 3c 20 02 00{{[ \t]+}}DIV{{[ \t]+}}@0x0200,R0
; DISASM: {{[0-9a-f]+}}: 3c 95{{[ \t]+}}DIV{{[ \t]+}}*R5,R2
; DISASM: {{[0-9a-f]+}}: 3e 36{{[ \t]+}}DIV{{[ \t]+}}*R6+,R8
; DISASM: {{[0-9a-f]+}}: 3d 29 00 08{{[ \t]+}}DIV{{[ \t]+}}@8(R9),R4
