; RUN: llvm-mc -triple tms9900 -show-encoding < %s | FileCheck %s

  .globl cru_bit
  sbo cru_bit

; CHECK: SBO{{[ \t]+}}cru_bit
; CHECK: fixup A - offset: 0, value: cru_bit, kind: fixup_tms9900_8
