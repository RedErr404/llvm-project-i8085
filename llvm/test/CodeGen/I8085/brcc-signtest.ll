; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s

; A signed compare against 0 (x < 0 / x >= 0) is a pure sign test: ORA A sets S
; from the top bit, so JM/JP (or the conditional returns RM/RP) branch on it
; directly - no XRI/CPI bias, no diamond. For i16 only the high byte matters.

declare void @sink()

define void @i8_slt0(i8 %a) {
; CHECK-LABEL: i8_slt0:
; CHECK:       ORA A
; CHECK-NOT:   XRI
; CHECK-NOT:   CPI
entry:
  %c = icmp slt i8 %a, 0
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}

define void @i8_sge0(i8 %a) {
; CHECK-LABEL: i8_sge0:
; CHECK:       ORA A
; CHECK-NOT:   XRI
; CHECK-NOT:   CPI
entry:
  %c = icmp sge i8 %a, 0
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}

define void @i16_slt0(i16 %a) {
; CHECK-LABEL: i16_slt0:
; CHECK:       ORA A
; CHECK-NOT:   XRI
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

define void @i16_sge0(i16 %a) {
; CHECK-LABEL: i16_sge0:
; CHECK:       ORA A
; CHECK-NOT:   XRI
; CHECK-NOT:   SBI
entry:
  %c = icmp sge i16 %a, 0
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}
