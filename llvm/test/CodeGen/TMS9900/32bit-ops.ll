; RUN: llc -march=tms9900 -O0 < %s | FileCheck %s
;
; Test 32-bit (i32) operations on the 16-bit TMS9900.
; i32 values are split into two i16 halves: R0 (high) and R1 (low).

; --- i32 add: low half A, carry propagation ---
; CHECK-LABEL: add32:
; CHECK: A{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: B{{[ \t]+}}*R11

define i32 @add32(i32 %a, i32 %b) {
entry:
  %result = add i32 %a, %b
  ret i32 %result
}

; --- i32 sub ---
; CHECK-LABEL: sub32:
; CHECK: S{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: B{{[ \t]+}}*R11

define i32 @sub32(i32 %a, i32 %b) {
entry:
  %result = sub i32 %a, %b
  ret i32 %result
}

; --- Zero extend i16 to i32 ---
; The high half should be cleared.
; CHECK-LABEL: zext_i16:
; CHECK: CLR{{[ \t]+}}R0
; CHECK: B{{[ \t]+}}*R11

define i32 @zext_i16(i16 %a) {
entry:
  %result = zext i16 %a to i32
  ret i32 %result
}

; --- i32 left shift by 1 ---
; Uses SLA on low half, SRL+SOC to propagate carry to high half.
; CHECK-LABEL: shl32_1:
; CHECK: SLA{{[ \t]+}}R{{[0-9]+}},1
; CHECK: B{{[ \t]+}}*R11

define i32 @shl32_1(i32 %a) {
entry:
  %result = shl i32 %a, 1
  ret i32 %result
}

; --- i32 logical right shift by 1 ---
; CHECK-LABEL: lshr32_1:
; CHECK: SRL{{[ \t]+}}R{{[0-9]+}},1
; CHECK: B{{[ \t]+}}*R11

define i32 @lshr32_1(i32 %a) {
entry:
  %result = lshr i32 %a, 1
  ret i32 %result
}

; --- i32 arithmetic right shift by 1 ---
; The high half uses SRA (arithmetic) to preserve sign.
; CHECK-LABEL: ashr32_1:
; CHECK: SRA{{[ \t]+}}R{{[0-9]+}},1
; CHECK: B{{[ \t]+}}*R11

define i32 @ashr32_1(i32 %a) {
entry:
  %result = ashr i32 %a, 1
  ret i32 %result
}

; --- i32 variable shift calls runtime helper ---
; CHECK-LABEL: shl32_var:
; CHECK: BL{{[ \t]+}}@__ashlsi3
; CHECK: B{{[ \t]+}}*R11

define i32 @shl32_var(i32 %a, i16 %amt) {
entry:
  %shift = zext i16 %amt to i32
  %result = shl i32 %a, %shift
  ret i32 %result
}

; --- i32 and ---
; CHECK-LABEL: and32:
; CHECK: SZC
; CHECK: B{{[ \t]+}}*R11

define i32 @and32(i32 %a, i32 %b) {
entry:
  %result = and i32 %a, %b
  ret i32 %result
}

; --- i32 or ---
; CHECK-LABEL: or32:
; CHECK: SOC
; CHECK: B{{[ \t]+}}*R11

define i32 @or32(i32 %a, i32 %b) {
entry:
  %result = or i32 %a, %b
  ret i32 %result
}

; --- i32 xor ---
; CHECK-LABEL: xor32:
; CHECK: XOR
; CHECK: B{{[ \t]+}}*R11

define i32 @xor32(i32 %a, i32 %b) {
entry:
  %result = xor i32 %a, %b
  ret i32 %result
}
