; RUN: llvm-mc -filetype=obj -triple tms9900 < %s \
; RUN:   | llvm-objdump -d - | FileCheck %s

  .globl ext
  jmp ext

; CHECK: {{[0-9a-f]+}}: 10 ff{{[ \t]+}}NOP
