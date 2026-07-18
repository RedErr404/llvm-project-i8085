; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s

; 16-bit +/-1 lowers to a single INX/DCX (1 byte, no flags) on the value's own
; register pair, instead of the byte-wise ADD_16/SUB_16 sequence.

define i16 @inc(i16 %a) {
; CHECK-LABEL: inc:
; CHECK:       INX B
; CHECK-NOT:   ADD
; CHECK-NOT:   ADC
; CHECK:       RET
entry:
  %r = add i16 %a, 1
  ret i16 %r
}

define i16 @dec_sub(i16 %a) {
; CHECK-LABEL: dec_sub:
; CHECK:       DCX B
; CHECK-NOT:   SUB
; CHECK-NOT:   SBB
; CHECK:       RET
entry:
  %r = sub i16 %a, 1
  ret i16 %r
}

define i16 @dec_addneg(i16 %a) {
; CHECK-LABEL: dec_addneg:
; CHECK:       DCX B
; CHECK-NOT:   SUB
; CHECK:       RET
entry:
  %r = add i16 %a, -1
  ret i16 %r
}

; +2 is not a single INX (still the byte-wise add): guard against over-matching.
define i16 @plus2(i16 %a) {
; CHECK-LABEL: plus2:
; CHECK-NOT:   INX B
entry:
  %r = add i16 %a, 2
  ret i16 %r
}
