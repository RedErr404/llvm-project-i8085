; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test byte (i8) arithmetic operations.
; On the TMS9900, i8 values are promoted to i16 for computation.

; --- Byte add (promoted to word) ---
; CHECK-LABEL: add_bytes:
; CHECK: A{{[ \t]+}}R1,R0
; CHECK: B{{[ \t]+}}*R11

define i8 @add_bytes(i8 %a, i8 %b) {
  %r = add i8 %a, %b
  ret i8 %r
}

; --- Byte sub ---
; CHECK-LABEL: sub_bytes:
; CHECK: S{{[ \t]+}}R1,R0
; CHECK: B{{[ \t]+}}*R11

define i8 @sub_bytes(i8 %a, i8 %b) {
  %r = sub i8 %a, %b
  ret i8 %r
}

; --- Byte AND ---
; CHECK-LABEL: and_bytes:
; CHECK: SZC
; CHECK: B{{[ \t]+}}*R11

define i8 @and_bytes(i8 %a, i8 %b) {
  %r = and i8 %a, %b
  ret i8 %r
}

; --- Byte OR ---
; CHECK-LABEL: or_bytes:
; CHECK: SOC{{[ \t]+}}R1,R0
; CHECK: B{{[ \t]+}}*R11

define i8 @or_bytes(i8 %a, i8 %b) {
  %r = or i8 %a, %b
  ret i8 %r
}

; --- Byte XOR ---
; CHECK-LABEL: xor_bytes:
; CHECK: XOR{{[ \t]+}}R1,R0
; CHECK: B{{[ \t]+}}*R11

define i8 @xor_bytes(i8 %a, i8 %b) {
  %r = xor i8 %a, %b
  ret i8 %r
}
