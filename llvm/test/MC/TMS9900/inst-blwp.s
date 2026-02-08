; RUN: llvm-mc -triple tms9900 -show-encoding < %s | FileCheck %s
; RUN: llvm-mc -filetype=obj -triple tms9900 < %s \
; RUN:   | llvm-objdump -d - | FileCheck --check-prefix=DISASM %s
;
; Test BLWP (Branch and Load Workspace Pointer) encoding.
; BLWP supports all general addressing modes (single-operand, format 3).

  blwp @0x0040
  blwp *r2
  blwp r3
  blwp *r4+
  blwp @8(r5)

; CHECK: BLWP{{[ \t]+}}@0x0040{{[ \t]+}}; encoding: [0x04,0x20,0x00,0x40]
; CHECK: BLWP{{[ \t]+}}*R2{{[ \t]+}}; encoding: [0x04,0x12]
; CHECK: BLWP{{[ \t]+}}R3{{[ \t]+}}; encoding: [0x04,0x03]
; CHECK: BLWP{{[ \t]+}}*R4+{{[ \t]+}}; encoding: [0x04,0x34]
; CHECK: BLWP{{[ \t]+}}@8(R5){{[ \t]+}}; encoding: [0x04,0x25,0x00,0x08]

; DISASM: {{[0-9a-f]+}}: 04 20 00 40{{[ \t]+}}BLWP{{[ \t]+}}@0x0040
; DISASM: {{[0-9a-f]+}}: 04 12{{[ \t]+}}BLWP{{[ \t]+}}*R2
; DISASM: {{[0-9a-f]+}}: 04 03{{[ \t]+}}BLWP{{[ \t]+}}R3
; DISASM: {{[0-9a-f]+}}: 04 34{{[ \t]+}}BLWP{{[ \t]+}}*R4+
; DISASM: {{[0-9a-f]+}}: 04 25 00 08{{[ \t]+}}BLWP{{[ \t]+}}@8(R5)
