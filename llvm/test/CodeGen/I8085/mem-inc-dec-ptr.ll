; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s

; (*p)++ / (*p)-- through a pointer held in a register should move the pointer
; into HL and use INR M / DCR M, not a LDAX/STAX load-modify-store.

define void @inc_ptr(ptr %p) {
; CHECK-LABEL: inc_ptr:
; CHECK:      INR M
; CHECK-NOT:  LDAX
; CHECK-NOT:  STAX
  %v = load i8, ptr %p
  %i = add i8 %v, 1
  store i8 %i, ptr %p
  ret void
}

define void @dec_ptr(ptr %p) {
; CHECK-LABEL: dec_ptr:
; CHECK:      DCR M
; CHECK-NOT:  LDAX
; CHECK-NOT:  STAX
  %v = load i8, ptr %p
  %d = sub i8 %v, 1
  store i8 %d, ptr %p
  ret void
}
