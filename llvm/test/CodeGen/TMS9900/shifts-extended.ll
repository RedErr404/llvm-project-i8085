; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Extended shift tests: variable amounts and special shift amounts.
; The existing shifts.ll tests constant shifts by 3, 4, 5.
; This file covers variable shifts and shift-by-8 (byte swap).

; --- Variable left shift: SLA Rx,0 (0 = use R0 for count) ---
; CHECK-LABEL: shl_var:
; CHECK: SLA{{[ \t]+}}R{{[0-9]+}},0
; CHECK: B{{[ \t]+}}*R11

define i16 @shl_var(i16 %a, i16 %amt) {
  %r = shl i16 %a, %amt
  ret i16 %r
}

; --- Variable arithmetic right shift ---
; CHECK-LABEL: sra_var:
; CHECK: SRA{{[ \t]+}}R{{[0-9]+}},0
; CHECK: B{{[ \t]+}}*R11

define i16 @sra_var(i16 %a, i16 %amt) {
  %r = ashr i16 %a, %amt
  ret i16 %r
}

; --- Variable logical right shift ---
; CHECK-LABEL: srl_var:
; CHECK: SRL{{[ \t]+}}R{{[0-9]+}},0
; CHECK: B{{[ \t]+}}*R11

define i16 @srl_var(i16 %a, i16 %amt) {
  %r = lshr i16 %a, %amt
  ret i16 %r
}

; --- Shift left by 8 ---
; CHECK-LABEL: shl8:
; CHECK: SLA{{[ \t]+}}R0,8
; CHECK: B{{[ \t]+}}*R11

define i16 @shl8(i16 %a) {
  %r = shl i16 %a, 8
  ret i16 %r
}

; --- Shift right by 8 ---
; CHECK-LABEL: srl8:
; CHECK: SRL{{[ \t]+}}R0,8
; CHECK: B{{[ \t]+}}*R11

define i16 @srl8(i16 %a) {
  %r = lshr i16 %a, 8
  ret i16 %r
}

; --- Shift left by 1 ---
; CHECK-LABEL: shl1:
; CHECK: SLA{{[ \t]+}}R0,1
; CHECK: B{{[ \t]+}}*R11

define i16 @shl1(i16 %a) {
  %r = shl i16 %a, 1
  ret i16 %r
}

; --- Arithmetic shift right by 1 (sign-preserving) ---
; CHECK-LABEL: sra1:
; CHECK: SRA{{[ \t]+}}R0,1
; CHECK: B{{[ \t]+}}*R11

define i16 @sra1(i16 %a) {
  %r = ashr i16 %a, 1
  ret i16 %r
}
