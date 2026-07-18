; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s

; i16 compares against a constant feeding a branch fuse to a linear byte-level
; sequence with immediates (SUI/SBI for magnitude, XRI combine for equality,
; high-byte bias for signed) instead of the SET_*_16 boolean diamond.

declare void @sink()

define void @weq_c(i16 %a) {
; CHECK-LABEL: weq_c:
; CHECK:       XRI
; CHECK-NOT:   ORA A
entry:
  %c = icmp eq i16 %a, 4660
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}

define void @wult_c(i16 %a) {
; CHECK-LABEL: wult_c:
; CHECK:       SUI
; CHECK:       SBI
; CHECK-NOT:   ORA A
entry:
  %c = icmp ult i16 %a, 1000
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}

; a < 0 (i16) is a sign test on the high byte (ORA A ; JM/JP).
define void @wslt0_c(i16 %a) {
; CHECK-LABEL: wslt0_c:
; CHECK:       ORA A
; CHECK-NOT:   SBI
entry:
  %c = icmp slt i16 %a, 0
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}

define void @wsgt_c(i16 %a) {
; CHECK-LABEL: wsgt_c:
; CHECK:       XRI 128
; CHECK:       SBI
; CHECK-NOT:   ORA A
entry:
  %c = icmp sgt i16 %a, 100
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}
