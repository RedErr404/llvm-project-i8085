; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test type conversions: sign-extend, zero-extend, truncate.

; --- Sign-extend i8 to i16 ---
; Uses SLA 8 + SRA 8 to shift the byte into position and sign-extend.
; CHECK-LABEL: sext_i8_to_i16:
; CHECK: SLA{{[ \t]+}}R0,8
; CHECK: SRA{{[ \t]+}}R0,8
; CHECK: B{{[ \t]+}}*R11

define i16 @sext_i8_to_i16(i8 %a) {
  %r = sext i8 %a to i16
  ret i16 %r
}

; --- Zero-extend i8 to i16 ---
; Uses ANDI to mask off the upper byte.
; CHECK-LABEL: zext_i8_to_i16:
; CHECK: ANDI{{[ \t]+}}R0,255
; CHECK: B{{[ \t]+}}*R11

define i16 @zext_i8_to_i16(i8 %a) {
  %r = zext i8 %a to i16
  ret i16 %r
}

; --- Truncate i16 to i8 ---
; No operation needed at register level; just use the low byte.
; CHECK-LABEL: trunc_i16_to_i8:
; CHECK: B{{[ \t]+}}*R11

define i8 @trunc_i16_to_i8(i16 %a) {
  %r = trunc i16 %a to i8
  ret i8 %r
}

; --- Zero-extend i16 to i32 ---
; High half gets CLR, low half stays in place.
; CHECK-LABEL: zext_i16_to_i32:
; CHECK: MOV{{[ \t]+}}R0,R1
; CHECK: CLR{{[ \t]+}}R0
; CHECK: B{{[ \t]+}}*R11

define i32 @zext_i16_to_i32(i16 %a) {
  %r = zext i16 %a to i32
  ret i32 %r
}

; --- Truncate i32 to i16 ---
; Just return the low half (R1), discarding R0 (high).
; CHECK-LABEL: trunc_i32_to_i16:
; CHECK: MOV{{[ \t]+}}R1,R0
; CHECK: B{{[ \t]+}}*R11

define i16 @trunc_i32_to_i16(i32 %a) {
  %r = trunc i32 %a to i16
  ret i16 %r
}
