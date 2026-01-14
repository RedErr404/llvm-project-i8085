; RUN: llvm-mc -triple tms9900 -show-encoding < %s | FileCheck %s
; RUN: llvm-mc -filetype=obj -triple tms9900 < %s \
; RUN:   | llvm-objdump -d - | FileCheck --check-prefix=DISASM %s

  mov r1, r2
  mov *r3, r4
  mov r5, *r6
  mov *r7+, r8
  mov r9, *r10+
  mov @0x1234, r1
  mov r2, @0x1234
  mov @16(r3), r4
  mov r5, @16(r6)

  movb r1, r2
  movb *r1, r2
  movb r3, *r4
  movb *r5+, r6
  movb r7, *r8+
  movb @0x1234, r9
  movb r10, @0x1234
  movb @4(r11), r12
  movb r13, @8(r14)

  a r1, r2
  s r3, r4
  c r5, r6
  soc r7, r8
  szc r9, r10
  ab r3, r4
  sb r5, r6
  cb r7, r8
  socb r9, r10
  szcb r11, r12

; CHECK: MOV{{[ \t]+}}R1,R2{{[ \t]+}}; encoding: [0xc0,0x81]
; CHECK: MOV{{[ \t]+}}*R3,R4{{[ \t]+}}; encoding: [0xc1,0x13]
; CHECK: MOV{{[ \t]+}}R5,*R6{{[ \t]+}}; encoding: [0xc5,0x85]
; CHECK: MOV{{[ \t]+}}*R7+,R8{{[ \t]+}}; encoding: [0xc2,0x37]
; CHECK: MOV{{[ \t]+}}R9,*R10+{{[ \t]+}}; encoding: [0xce,0x89]
; CHECK: MOV{{[ \t]+}}@0x1234,R1{{[ \t]+}}; encoding: [0xc0,0x60,0x12,0x34]
; CHECK: MOV{{[ \t]+}}R2,@0x1234{{[ \t]+}}; encoding: [0xc8,0x02,0x12,0x34]
; CHECK: MOV{{[ \t]+}}@16(R3),R4{{[ \t]+}}; encoding: [0xc1,0x23,0x00,0x10]
; CHECK: MOV{{[ \t]+}}R5,@16(R6){{[ \t]+}}; encoding: [0xc9,0x85,0x00,0x10]

; CHECK: MOVB{{[ \t]+}}R1,R2{{[ \t]+}}; encoding: [0xd0,0x81]
; CHECK: MOVB{{[ \t]+}}*R1,R2{{[ \t]+}}; encoding: [0xd0,0x91]
; CHECK: MOVB{{[ \t]+}}R3,*R4{{[ \t]+}}; encoding: [0xd5,0x03]
; CHECK: MOVB{{[ \t]+}}*R5+,R6{{[ \t]+}}; encoding: [0xd1,0xb5]
; CHECK: MOVB{{[ \t]+}}R7,*R8+{{[ \t]+}}; encoding: [0xde,0x07]
; CHECK: MOVB{{[ \t]+}}@0x1234,R9{{[ \t]+}}; encoding: [0xd2,0x60,0x12,0x34]
; CHECK: MOVB{{[ \t]+}}R10,@0x1234{{[ \t]+}}; encoding: [0xd8,0x0a,0x12,0x34]
; CHECK: MOVB{{[ \t]+}}@4(R11),R12{{[ \t]+}}; encoding: [0xd3,0x2b,0x00,0x04]
; CHECK: MOVB{{[ \t]+}}R13,@8(R14){{[ \t]+}}; encoding: [0xdb,0x8d,0x00,0x08]

; CHECK: A{{[ \t]+}}R1,R2{{[ \t]+}}; encoding: [0xa0,0x81]
; CHECK: S{{[ \t]+}}R3,R4{{[ \t]+}}; encoding: [0x61,0x03]
; CHECK: C{{[ \t]+}}R5,R6{{[ \t]+}}; encoding: [0x81,0x46]
; CHECK: SOC{{[ \t]+}}R7,R8{{[ \t]+}}; encoding: [0xe2,0x07]
; CHECK: SZC{{[ \t]+}}R9,R10{{[ \t]+}}; encoding: [0x42,0x89]
; CHECK: AB{{[ \t]+}}R3,R4{{[ \t]+}}; encoding: [0xb1,0x03]
; CHECK: SB{{[ \t]+}}R5,R6{{[ \t]+}}; encoding: [0x71,0x85]
; CHECK: CB{{[ \t]+}}R7,R8{{[ \t]+}}; encoding: [0x91,0xc8]
; CHECK: SOCB{{[ \t]+}}R9,R10{{[ \t]+}}; encoding: [0xf2,0x89]
; CHECK: SZCB{{[ \t]+}}R11,R12{{[ \t]+}}; encoding: [0x53,0x0b]

; DISASM: {{[0-9a-f]+}}: c0 81{{[ \t]+}}MOV{{[ \t]+}}R1,R2
; DISASM: {{[0-9a-f]+}}: c1 13{{[ \t]+}}MOV{{[ \t]+}}*R3,R4
; DISASM: {{[0-9a-f]+}}: c5 85{{[ \t]+}}MOV{{[ \t]+}}R5,*R6
; DISASM: {{[0-9a-f]+}}: c2 37{{[ \t]+}}MOV{{[ \t]+}}*R7+,R8
; DISASM: {{[0-9a-f]+}}: ce 89{{[ \t]+}}MOV{{[ \t]+}}R9,*R10+
; DISASM: {{[0-9a-f]+}}: c0 60 12 34{{[ \t]+}}MOV{{[ \t]+}}@0x1234,R1
; DISASM: {{[0-9a-f]+}}: c8 02 12 34{{[ \t]+}}MOV{{[ \t]+}}R2,@0x1234
; DISASM: {{[0-9a-f]+}}: c1 23 00 10{{[ \t]+}}MOV{{[ \t]+}}@16(R3),R4
; DISASM: {{[0-9a-f]+}}: c9 85 00 10{{[ \t]+}}MOV{{[ \t]+}}R5,@16(R6)

; DISASM: {{[0-9a-f]+}}: d0 81{{[ \t]+}}MOVB{{[ \t]+}}R1,R2
; DISASM: {{[0-9a-f]+}}: d0 91{{[ \t]+}}MOVB{{[ \t]+}}*R1,R2
; DISASM: {{[0-9a-f]+}}: d5 03{{[ \t]+}}MOVB{{[ \t]+}}R3,*R4
; DISASM: {{[0-9a-f]+}}: d1 b5{{[ \t]+}}MOVB{{[ \t]+}}*R5+,R6
; DISASM: {{[0-9a-f]+}}: de 07{{[ \t]+}}MOVB{{[ \t]+}}R7,*R8+
; DISASM: {{[0-9a-f]+}}: d2 60 12 34{{[ \t]+}}MOVB{{[ \t]+}}@0x1234,R9
; DISASM: {{[0-9a-f]+}}: d8 0a 12 34{{[ \t]+}}MOVB{{[ \t]+}}R10,@0x1234
; DISASM: {{[0-9a-f]+}}: d3 2b 00 04{{[ \t]+}}MOVB{{[ \t]+}}@4(R11),R12
; DISASM: {{[0-9a-f]+}}: db 8d 00 08{{[ \t]+}}MOVB{{[ \t]+}}R13,@8(R14)

; DISASM: {{[0-9a-f]+}}: a0 81{{[ \t]+}}A{{[ \t]+}}R1,R2
; DISASM: {{[0-9a-f]+}}: 61 03{{[ \t]+}}S{{[ \t]+}}R3,R4
; DISASM: {{[0-9a-f]+}}: 81 46{{[ \t]+}}C{{[ \t]+}}R5,R6
; DISASM: {{[0-9a-f]+}}: e2 07{{[ \t]+}}SOC{{[ \t]+}}R7,R8
; DISASM: {{[0-9a-f]+}}: 42 89{{[ \t]+}}SZC{{[ \t]+}}R9,R10
; DISASM: {{[0-9a-f]+}}: b1 03{{[ \t]+}}AB{{[ \t]+}}R3,R4
; DISASM: {{[0-9a-f]+}}: 71 85{{[ \t]+}}SB{{[ \t]+}}R5,R6
; DISASM: {{[0-9a-f]+}}: 91 c8{{[ \t]+}}CB{{[ \t]+}}R7,R8
; DISASM: {{[0-9a-f]+}}: f2 89{{[ \t]+}}SOCB{{[ \t]+}}R9,R10
; DISASM: {{[0-9a-f]+}}: 53 0b{{[ \t]+}}SZCB{{[ \t]+}}R11,R12
