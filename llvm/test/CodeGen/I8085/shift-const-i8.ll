; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s

; Constant i8 shifts by 5/6/7 have compile-time-known amounts and must be
; branchless. Previously they fell through to the runtime count-down shift loop
; (SHL_8/SRL_8/SRA_8). A left/right shift by k on the 8085 has no barrel-shift
; instruction, so the cheapest form is to rotate the accumulator the *short* way
; (8-k positions) and mask off the bits that wrapped around.

; shl by k == rotate-right by (8-k), then keep the high k..7 bits.
define i8 @shl_i8_7(i8 %x) {
; CHECK-LABEL: shl_i8_7:
; CHECK:      MOV A,
; CHECK-NEXT: RRC
; CHECK-NEXT: ANI 128
  %r = shl i8 %x, 7
  ret i8 %r
}

define i8 @shl_i8_6(i8 %x) {
; CHECK-LABEL: shl_i8_6:
; CHECK:      MOV A,
; CHECK-NEXT: RRC
; CHECK-NEXT: RRC
; CHECK-NEXT: ANI 192
  %r = shl i8 %x, 6
  ret i8 %r
}

define i8 @shl_i8_5(i8 %x) {
; CHECK-LABEL: shl_i8_5:
; CHECK:      MOV A,
; CHECK-NEXT: RRC
; CHECK-NEXT: RRC
; CHECK-NEXT: RRC
; CHECK-NEXT: ANI 224
  %r = shl i8 %x, 5
  ret i8 %r
}

; lshr by k == rotate-left by (8-k), then keep the low 0..(7-k) bits.
define i8 @srl_i8_7(i8 %x) {
; CHECK-LABEL: srl_i8_7:
; CHECK:      MOV A,
; CHECK-NEXT: RLC
; CHECK-NEXT: ANI 1
  %r = lshr i8 %x, 7
  ret i8 %r
}

define i8 @srl_i8_5(i8 %x) {
; CHECK-LABEL: srl_i8_5:
; CHECK:      MOV A,
; CHECK-NEXT: RLC
; CHECK-NEXT: RLC
; CHECK-NEXT: RLC
; CHECK-NEXT: ANI 7
  %r = lshr i8 %x, 5
  ret i8 %r
}

; ashr by 5/6/7 just needs to stop being a runtime loop (straight-line unroll).
define i8 @sra_i8_7(i8 %x) {
; CHECK-LABEL: sra_i8_7:
; CHECK-NOT:  JP
; CHECK:      RAR
  %r = ashr i8 %x, 7
  ret i8 %r
}
