; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test basic ALU operations: add, sub, and, or, xor, neg, inv, mul.

; --- 16-bit add ---
; CHECK-LABEL: add16:
; CHECK: A{{[ \t]+}}R1,R0
; CHECK: B{{[ \t]+}}*R11

define i16 @add16(i16 %a, i16 %b) {
  %r = add i16 %a, %b
  ret i16 %r
}

; --- 16-bit sub ---
; CHECK-LABEL: sub16:
; CHECK: S{{[ \t]+}}R1,R0
; CHECK: B{{[ \t]+}}*R11

define i16 @sub16(i16 %a, i16 %b) {
  %r = sub i16 %a, %b
  ret i16 %r
}

; --- 16-bit or uses SOC ---
; CHECK-LABEL: or16:
; CHECK: SOC{{[ \t]+}}R1,R0
; CHECK: B{{[ \t]+}}*R11

define i16 @or16(i16 %a, i16 %b) {
  %r = or i16 %a, %b
  ret i16 %r
}

; --- 16-bit xor ---
; CHECK-LABEL: xor16:
; CHECK: XOR{{[ \t]+}}R1,R0
; CHECK: B{{[ \t]+}}*R11

define i16 @xor16(i16 %a, i16 %b) {
  %r = xor i16 %a, %b
  ret i16 %r
}

; --- 16-bit negate ---
; CHECK-LABEL: neg16:
; CHECK: NEG{{[ \t]+}}R0
; CHECK: B{{[ \t]+}}*R11

define i16 @neg16(i16 %a) {
  %r = sub i16 0, %a
  ret i16 %r
}

; --- 16-bit bitwise NOT uses INV ---
; CHECK-LABEL: inv16:
; CHECK: INV{{[ \t]+}}R0
; CHECK: B{{[ \t]+}}*R11

define i16 @inv16(i16 %a) {
  %r = xor i16 %a, -1
  ret i16 %r
}

; --- 16-bit multiply uses MPY ---
; CHECK-LABEL: mul16:
; CHECK: MPY
; CHECK: B{{[ \t]+}}*R11

define i16 @mul16(i16 %a, i16 %b) {
  %r = mul i16 %a, %b
  ret i16 %r
}

; --- 16-bit and uses INV+SZC (TMS9900 has no AND instruction for registers) ---
; CHECK-LABEL: and16:
; CHECK: INV
; CHECK: SZC
; CHECK: B{{[ \t]+}}*R11

define i16 @and16(i16 %a, i16 %b) {
  %r = and i16 %a, %b
  ret i16 %r
}
