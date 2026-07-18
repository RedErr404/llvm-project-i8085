; RUN: llc -mattr=i8085,sram < %s -march=i8085 | FileCheck %s

; Select with various comparison conditions on i16 operands.
; Adapted from RISC-V select-cc.ll patterns.
; The i16 condition is materialized branchlessly (byte-level SUB/SBB or XRA
; combine, then SBB A;ANI 1 / INR A) and the select branches on it via ORA A.

define i16 @select_eq_i16(i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: select_eq_i16:
; CHECK: XRA
; CHECK: ORA
; CHECK: SUI 1
; CHECK: SBB A
; CHECK: RET
entry:
  %cmp = icmp eq i16 %a, %b
  %sel = select i1 %cmp, i16 %c, i16 %d
  ret i16 %sel
}

define i16 @select_ne_i16(i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: select_ne_i16:
; CHECK: XRA
; CHECK: SUI 1
; CHECK: SBB A
; CHECK: INR A
; CHECK: RET
entry:
  %cmp = icmp ne i16 %a, %b
  %sel = select i1 %cmp, i16 %c, i16 %d
  ret i16 %sel
}

define i16 @select_slt_i16(i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: select_slt_i16:
; CHECK: XRI 128
; CHECK: SUB
; CHECK: SBB A
; CHECK: ANI 1
; CHECK: RET
entry:
  %cmp = icmp slt i16 %a, %b
  %sel = select i1 %cmp, i16 %c, i16 %d
  ret i16 %sel
}

define i16 @select_ult_i16(i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: select_ult_i16:
; CHECK: SUB
; CHECK: SBB A
; CHECK: ANI 1
; CHECK: RET
entry:
  %cmp = icmp ult i16 %a, %b
  %sel = select i1 %cmp, i16 %c, i16 %d
  ret i16 %sel
}

define i8 @select_eq_i8(i8 %a, i8 %b, i8 %c, i8 %d) {
; CHECK-LABEL: select_eq_i8:
; CHECK: SUB
; CHECK: JNZ
; CHECK: LDAX B
; CHECK: RET
entry:
  %cmp = icmp eq i8 %a, %b
  %sel = select i1 %cmp, i8 %c, i8 %d
  ret i8 %sel
}

define i16 @select_sge_i16(i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: select_sge_i16:
; CHECK: XRI 128
; CHECK: SUB
; CHECK: SBB A
; CHECK: INR A
; CHECK: RET
entry:
  %cmp = icmp sge i16 %a, %b
  %sel = select i1 %cmp, i16 %c, i16 %d
  ret i16 %sel
}
