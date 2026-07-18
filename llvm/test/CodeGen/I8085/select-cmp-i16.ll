; RUN: llc -mattr=i8085,sram < %s -march=i8085 | FileCheck %s

; Select with various comparison conditions on i16 operands.
; Adapted from RISC-V select-cc.ll patterns.

define i16 @select_eq_i16(i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: select_eq_i16:
; CHECK: CMP D
; CHECK: JNZ
; CHECK: CMP E
; CHECK: RET
entry:
  %cmp = icmp eq i16 %a, %b
  %sel = select i1 %cmp, i16 %c, i16 %d
  ret i16 %sel
}

define i16 @select_ne_i16(i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: select_ne_i16:
; CHECK: CMP D
; CHECK: JNZ
; CHECK: CMP E
; CHECK: RET
entry:
  %cmp = icmp ne i16 %a, %b
  %sel = select i1 %cmp, i16 %c, i16 %d
  ret i16 %sel
}

define i16 @select_slt_i16(i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: select_slt_i16:
; CHECK: XRA D
; CHECK: ANI 128
; CHECK: SBB
; CHECK: RET
entry:
  %cmp = icmp slt i16 %a, %b
  %sel = select i1 %cmp, i16 %c, i16 %d
  ret i16 %sel
}

define i16 @select_ult_i16(i16 %a, i16 %b, i16 %c, i16 %d) {
; CHECK-LABEL: select_ult_i16:
; CHECK: SUB
; CHECK: SBB
; CHECK: JNC
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
; CHECK: XRA
; CHECK: ANI 128
; CHECK: SBB
; CHECK: RET
entry:
  %cmp = icmp sge i16 %a, %b
  %sel = select i1 %cmp, i16 %c, i16 %d
  ret i16 %sel
}
