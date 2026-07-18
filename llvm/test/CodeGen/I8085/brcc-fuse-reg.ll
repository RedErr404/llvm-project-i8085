; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s

; A register-RHS i8 compare feeding a branch should fuse to
; MOV A,lhs ; SUB rhs ; Jcc  -- NOT the SET_*_8 boolean diamond that
; materializes 0/1 (MVI ...,1) and then re-tests it (ORA A ; JNZ ...).
; So SUB must be present and the "ORA A" re-test must be gone.

declare void @sink()

define void @eq_reg(i8 %a, i8 %b) {
; CHECK-LABEL: eq_reg:
; CHECK:       SUB
; CHECK-NOT:   ORA A
; CHECK-NOT:   SUB A
entry:
  %c = icmp eq i8 %a, %b
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}

define void @ne_reg(i8 %a, i8 %b) {
; CHECK-LABEL: ne_reg:
; CHECK:       SUB
; CHECK-NOT:   ORA A
; CHECK-NOT:   SUB A
entry:
  %c = icmp ne i8 %a, %b
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}

define void @ult_reg(i8 %a, i8 %b) {
; CHECK-LABEL: ult_reg:
; CHECK:       SUB
; CHECK-NOT:   ORA A
; CHECK-NOT:   SUB A
entry:
  %c = icmp ult i8 %a, %b
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}

define void @uge_reg(i8 %a, i8 %b) {
; CHECK-LABEL: uge_reg:
; CHECK:       SUB
; CHECK-NOT:   ORA A
; CHECK-NOT:   SUB A
entry:
  %c = icmp uge i8 %a, %b
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}
