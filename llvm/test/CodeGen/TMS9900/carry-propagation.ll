; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test 32-bit addition and subtraction carry/borrow propagation.
; TMS9900 has no ADC/SBC instructions, so the compiler must synthesize
; carry detection using compare-after-add (unsigned overflow check).
;
; Pattern for add: add low words, compare result to original to detect
; carry (JHE = no overflow), then add high words + carry.
;
; Pattern for sub: compare low words to detect borrow before subtracting,
; then subtract high words + borrow.

target datalayout = "e-m:e-p:16:16-i16:16-a:0:16-n16"

; --- 32-bit add with carry propagation ---
; Low word add followed by carry detection and high word add.
; CHECK-LABEL: add32:
; CHECK: A{{[ \t]+}}R{{[0-9]+}},R1
; CHECK: C{{[ \t]+}}R{{[0-9]+}},R1
; CHECK: JHE
; CHECK: A{{[ \t]+}}R2,R0
; CHECK: B{{[ \t]+}}*R11

define i32 @add32(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

; --- 32-bit sub with borrow propagation ---
; Compare low words to detect borrow, then subtract high words + borrow.
; CHECK-LABEL: sub32:
; CHECK: C{{[ \t]+}}R3,R1
; CHECK: JHE
; CHECK: S{{[ \t]+}}R2,R0
; CHECK: S{{[ \t]+}}R3,R1
; CHECK: B{{[ \t]+}}*R11

define i32 @sub32(i32 %a, i32 %b) {
  %r = sub i32 %a, %b
  ret i32 %r
}

; --- 32-bit add with constant (0x10001 = 65537) ---
; Low word gets +1 (INC), high word gets +1 plus carry.
; CHECK-LABEL: add32_const:
; CHECK: INC{{[ \t]+}}R1
; CHECK: INC{{[ \t]+}}R0
; CHECK: B{{[ \t]+}}*R11

define i32 @add32_const(i32 %a) {
  %r = add i32 %a, 65537
  ret i32 %r
}

; --- 32-bit sub with constant (0x10001 = 65537) ---
; Low word gets -1 (DEC), high word gets subtracted with borrow.
; CHECK-LABEL: sub32_const:
; CHECK: DEC{{[ \t]+}}R1
; CHECK: DECT{{[ \t]+}}R0
; CHECK: B{{[ \t]+}}*R11

define i32 @sub32_const(i32 %a) {
  %r = sub i32 %a, 65537
  ret i32 %r
}
