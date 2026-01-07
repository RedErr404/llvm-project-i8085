; RUN: llvm-mc -triple tms9900 -show-encoding < %s | FileCheck %s
; RUN: llvm-mc -filetype=obj -triple tms9900 < %s \
; RUN:   | llvm-objdump -d - | FileCheck --check-prefix=DISASM %s

  li r1, 0x1234
  ai r2, 0x0100
  andi r3, 0x00ff
  ori r4, 0x0f0f
  ci r5, 0x2222

  stst r1
  stwp r2
  lwpi 0x1000
  limi 0x0004
  rtwp
  idle
  rset
  ckof
  ckon
  lrex

; CHECK: LI{{[ \t]+}}R1,4660{{[ \t]+}}; encoding: [0x02,0x01,0x12,0x34]
; CHECK: AI{{[ \t]+}}R2,256{{[ \t]+}}; encoding: [0x02,0x22,0x01,0x00]
; CHECK: ANDI{{[ \t]+}}R3,255{{[ \t]+}}; encoding: [0x02,0x43,0x00,0xff]
; CHECK: ORI{{[ \t]+}}R4,3855{{[ \t]+}}; encoding: [0x02,0x64,0x0f,0x0f]
; CHECK: CI{{[ \t]+}}R5,8738{{[ \t]+}}; encoding: [0x02,0x85,0x22,0x22]

; CHECK: STST{{[ \t]+}}R1{{[ \t]+}}; encoding: [0x02,0xc1]
; CHECK: STWP{{[ \t]+}}R2{{[ \t]+}}; encoding: [0x02,0xa2]
; CHECK: LWPI{{[ \t]+}}4096{{[ \t]+}}; encoding: [0x02,0xe0,0x10,0x00]
; CHECK: LIMI{{[ \t]+}}4{{[ \t]+}}; encoding: [0x03,0x00,0x00,0x04]
; CHECK: RTWP{{[ \t]+}}; encoding: [0x03,0x80]
; CHECK: IDLE{{[ \t]+}}; encoding: [0x03,0x40]
; CHECK: RSET{{[ \t]+}}; encoding: [0x03,0x60]
; CHECK: CKOF{{[ \t]+}}; encoding: [0x03,0xc0]
; CHECK: CKON{{[ \t]+}}; encoding: [0x03,0xa0]
; CHECK: LREX{{[ \t]+}}; encoding: [0x03,0xe0]

; DISASM: {{[0-9a-f]+}}: 02 01 12 34{{[ \t]+}}LI{{[ \t]+}}R1,4660
; DISASM: {{[0-9a-f]+}}: 02 22 01 00{{[ \t]+}}AI{{[ \t]+}}R2,256
; DISASM: {{[0-9a-f]+}}: 02 43 00 ff{{[ \t]+}}ANDI{{[ \t]+}}R3,255
; DISASM: {{[0-9a-f]+}}: 02 64 0f 0f{{[ \t]+}}ORI{{[ \t]+}}R4,3855
; DISASM: {{[0-9a-f]+}}: 02 85 22 22{{[ \t]+}}CI{{[ \t]+}}R5,8738

; DISASM: {{[0-9a-f]+}}: 02 c1{{[ \t]+}}STST{{[ \t]+}}R1
; DISASM: {{[0-9a-f]+}}: 02 a2{{[ \t]+}}STWP{{[ \t]+}}R2
; DISASM: {{[0-9a-f]+}}: 02 e0 10 00{{[ \t]+}}LWPI{{[ \t]+}}4096
; DISASM: {{[0-9a-f]+}}: 03 00 00 04{{[ \t]+}}LIMI{{[ \t]+}}4
; DISASM: {{[0-9a-f]+}}: 03 80{{[ \t]+}}RTWP
; DISASM: {{[0-9a-f]+}}: 03 40{{[ \t]+}}IDLE
; DISASM: {{[0-9a-f]+}}: 03 60{{[ \t]+}}RSET
; DISASM: {{[0-9a-f]+}}: 03 c0{{[ \t]+}}CKOF
; DISASM: {{[0-9a-f]+}}: 03 a0{{[ \t]+}}CKON
; DISASM: {{[0-9a-f]+}}: 03 e0{{[ \t]+}}LREX
