; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s

; i16 compares feeding a branch fuse to a linear byte-level sequence (SUB/SBB
; for magnitude, XRA/ORA for equality, XRI 128 bias for signed) instead of the
; SET_*_16 boolean diamond that re-tests with "ORA A".

declare void @sink()

define void @weq(i16 %a, i16 %b) {
; CHECK-LABEL: weq:
; CHECK:       XRA
; CHECK-NOT:   ORA A
entry:
  %c = icmp eq i16 %a, %b
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}

define void @wult(i16 %a, i16 %b) {
; CHECK-LABEL: wult:
; CHECK:       SUB
; CHECK:       SBB
; CHECK-NOT:   ORA A
entry:
  %c = icmp ult i16 %a, %b
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}

define void @wuge(i16 %a, i16 %b) {
; CHECK-LABEL: wuge:
; CHECK:       SBB
; CHECK-NOT:   ORA A
entry:
  %c = icmp uge i16 %a, %b
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}

define void @wslt(i16 %a, i16 %b) {
; CHECK-LABEL: wslt:
; CHECK:       XRI 128
; CHECK:       SBB
; CHECK-NOT:   ORA A
entry:
  %c = icmp slt i16 %a, %b
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}

define void @wsge(i16 %a, i16 %b) {
; CHECK-LABEL: wsge:
; CHECK:       XRI 128
; CHECK:       SBB
; CHECK-NOT:   ORA A
entry:
  %c = icmp sge i16 %a, %b
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}
