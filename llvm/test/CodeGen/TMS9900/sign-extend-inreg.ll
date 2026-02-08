; RUN: llc -march=tms9900 -O2 < %s | FileCheck %s
;
; Test sign extension of narrow values within a register.
; SIGN_EXTEND_INREG for i8 and i1 are both Expand on TMS9900.
; i8: generates SLA 8 + SRA 8 (shift to sign position, arithmetic shift back).
; i1: generates ANDI 1 + NEG (mask to single bit, negate for sign extension).

; --- Sign extend i8 within i16 register ---
; Truncate to i8 then sign-extend back: SLA 8 shifts byte to MSB, SRA 8
; does arithmetic shift right to fill with sign bit.
; CHECK-LABEL: sext_i8:
; CHECK: SLA{{[ \t]+}}R0,8
; CHECK: SRA{{[ \t]+}}R0,8
; CHECK: B{{[ \t]+}}*R11

define i16 @sext_i8(i16 %x) {
  %trunc = trunc i16 %x to i8
  %ext = sext i8 %trunc to i16
  ret i16 %ext
}

; --- Sign extend i1 within i16 register ---
; Truncate to i1 then sign-extend: mask to bit 0 then negate
; (0 stays 0, 1 becomes 0xFFFF i.e. -1).
; CHECK-LABEL: sext_i1:
; CHECK: ANDI{{[ \t]+}}R0,1
; CHECK: NEG{{[ \t]+}}R0
; CHECK: B{{[ \t]+}}*R11

define i16 @sext_i1(i16 %x) {
  %trunc = trunc i16 %x to i1
  %ext = sext i1 %trunc to i16
  ret i16 %ext
}
