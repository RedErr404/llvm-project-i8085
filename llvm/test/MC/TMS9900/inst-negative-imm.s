; RUN: llvm-mc -triple tms9900 -show-encoding < %s | FileCheck %s
; RUN: llvm-mc -filetype=obj -triple tms9900 < %s \
; RUN:   | llvm-objdump -d - | FileCheck --check-prefix=DISASM %s
;
; Test negative immediate values in immediate-format instructions.
; The TMS9900 stores immediate values as 16-bit two's complement.

  nop
  ai r1, -1
  ai r2, -256
  ci r3, -100
  li r4, -32768
  li r5, -1

; NOP is JMP $+2 (offset 0)
; CHECK: NOP{{[ \t]+}}; encoding: [0x10,0x00]

; AI with -1 = 0xFFFF
; CHECK: AI{{[ \t]+}}R1,-1{{[ \t]+}}; encoding: [0x02,0x21,0xff,0xff]

; AI with -256 = 0xFF00
; CHECK: AI{{[ \t]+}}R2,-256{{[ \t]+}}; encoding: [0x02,0x22,0xff,0x00]

; CI with -100 = 0xFF9C
; CHECK: CI{{[ \t]+}}R3,-100{{[ \t]+}}; encoding: [0x02,0x83,0xff,0x9c]

; LI with -32768 = 0x8000
; CHECK: LI{{[ \t]+}}R4,-32768{{[ \t]+}}; encoding: [0x02,0x04,0x80,0x00]

; LI with -1 = 0xFFFF
; CHECK: LI{{[ \t]+}}R5,-1{{[ \t]+}}; encoding: [0x02,0x05,0xff,0xff]

; Disassembler prints unsigned equivalents for the immediate field.
; DISASM: {{[0-9a-f]+}}: 10 00{{[ \t]+}}NOP
; DISASM: {{[0-9a-f]+}}: 02 21 ff ff{{[ \t]+}}AI{{[ \t]+}}R1,65535
; DISASM: {{[0-9a-f]+}}: 02 22 ff 00{{[ \t]+}}AI{{[ \t]+}}R2,65280
; DISASM: {{[0-9a-f]+}}: 02 83 ff 9c{{[ \t]+}}CI{{[ \t]+}}R3,65436
; DISASM: {{[0-9a-f]+}}: 02 04 80 00{{[ \t]+}}LI{{[ \t]+}}R4,32768
; DISASM: {{[0-9a-f]+}}: 02 05 ff ff{{[ \t]+}}LI{{[ \t]+}}R5,65535
