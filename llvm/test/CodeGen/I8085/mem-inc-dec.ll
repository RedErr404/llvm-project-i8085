; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s

; A byte in memory incremented/decremented by 1 should use the 8085's in-place
; INR M / DCR M (read-modify-write at (HL), one byte, preserves CY), not a
; load / add-or-sub / store round trip.

@g = global i8 0

define void @inc_global() {
; CHECK-LABEL: inc_global:
; CHECK:      LXI H, g
; CHECK-NEXT: INR M
; CHECK-NEXT: RET
  %v = load i8, ptr @g
  %i = add i8 %v, 1
  store i8 %i, ptr @g
  ret void
}

define void @dec_global() {
; CHECK-LABEL: dec_global:
; CHECK:      LXI H, g
; CHECK-NEXT: DCR M
; CHECK-NEXT: RET
  %v = load i8, ptr @g
  %d = sub i8 %v, 1
  store i8 %d, ptr @g
  ret void
}

; Guard: a step other than +/-1 must NOT be turned into INR M / DCR M.
define void @add5_global() {
; CHECK-LABEL: add5_global:
; CHECK-NOT:  INR M
; CHECK-NOT:  DCR M
  %v = load i8, ptr @g
  %i = add i8 %v, 5
  store i8 %i, ptr @g
  ret void
}
