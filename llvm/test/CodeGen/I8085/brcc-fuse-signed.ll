; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s

; A signed i8 compare feeding a branch fuses to the sign-bias sequence
;   MOV A,rhs ; XRI 80h ; MOV t,A ; MOV A,lhs ; XRI 80h ; SUB t ; Jcc
; instead of the same-sign/different-sign boolean diamond. So we should see
; XRI 128 and SUB, and NOT the boolean re-test "ORA A".

declare void @sink()

define void @slt_reg(i8 %a, i8 %b) {
; CHECK-LABEL: slt_reg:
; CHECK:       XRI 128
; CHECK:       SUB
; CHECK-NOT:   ORA A
; CHECK-NOT:   SUB A
entry:
  %c = icmp slt i8 %a, %b
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}

define void @sge_reg(i8 %a, i8 %b) {
; CHECK-LABEL: sge_reg:
; CHECK:       XRI 128
; CHECK:       SUB
; CHECK-NOT:   ORA A
; CHECK-NOT:   SUB A
entry:
  %c = icmp sge i8 %a, %b
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}

define void @sgt_reg(i8 %a, i8 %b) {
; CHECK-LABEL: sgt_reg:
; CHECK:       XRI 128
; CHECK:       SUB
; CHECK-NOT:   ORA A
; CHECK-NOT:   SUB A
entry:
  %c = icmp sgt i8 %a, %b
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}

define void @sle_reg(i8 %a, i8 %b) {
; CHECK-LABEL: sle_reg:
; CHECK:       XRI 128
; CHECK:       SUB
; CHECK-NOT:   ORA A
; CHECK-NOT:   SUB A
entry:
  %c = icmp sle i8 %a, %b
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}
