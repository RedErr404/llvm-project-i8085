; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test 32-bit comparison patterns.
; On the 16-bit TMS9900, 32-bit comparisons require comparing both
; the high and low words. The patterns differ for equality vs ordering.

target datalayout = "e-m:e-p:16:16-i16:16-a:0:16-n16"

; --- 32-bit equality: compare both halves ---
; Both high and low words must match for equality.
; CHECK-LABEL: eq32:
; CHECK: C{{[ \t]+}}R3,R1
; CHECK: C{{[ \t]+}}R2,R0
; CHECK: B{{[ \t]+}}*R11

define i1 @eq32(i32 %a, i32 %b) {
  %r = icmp eq i32 %a, %b
  ret i1 %r
}

; --- 32-bit inequality: compare both halves ---
; Either high or low word mismatch means not-equal.
; CHECK-LABEL: ne32:
; CHECK: C{{[ \t]+}}R3,R1
; CHECK: C{{[ \t]+}}R2,R0
; CHECK: B{{[ \t]+}}*R11

define i1 @ne32(i32 %a, i32 %b) {
  %r = icmp ne i32 %a, %b
  ret i1 %r
}

; --- 32-bit signed less than ---
; Compare high words first; if equal, compare low words (unsigned).
; Uses JGT for signed comparison of high words.
; CHECK-LABEL: slt32:
; CHECK: C{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: C{{[ \t]+}}R2,R0
; CHECK: B{{[ \t]+}}*R11

define i1 @slt32(i32 %a, i32 %b) {
  %r = icmp slt i32 %a, %b
  ret i1 %r
}

; --- 32-bit unsigned less than ---
; Compare high words first; if equal, compare low words.
; Uses JHE for unsigned comparison.
; CHECK-LABEL: ult32:
; CHECK: C{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: JHE
; CHECK: C{{[ \t]+}}R2,R0
; CHECK: B{{[ \t]+}}*R11

define i1 @ult32(i32 %a, i32 %b) {
  %r = icmp ult i32 %a, %b
  ret i1 %r
}

; --- 32-bit equality used in select ---
; XOR both halves, OR results; if zero, they're equal.
; CHECK-LABEL: select_eq32:
; CHECK: XOR{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: XOR{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: SOC{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: B{{[ \t]+}}*R11

define i16 @select_eq32(i32 %a, i32 %b, i16 %x, i16 %y) {
  %cmp = icmp eq i32 %a, %b
  %sel = select i1 %cmp, i16 %x, i16 %y
  ret i16 %sel
}
