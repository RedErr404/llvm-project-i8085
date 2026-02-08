; RUN: llc -march=tms9900 -O2 < %s | FileCheck %s
;
; Test rotation operations.
; TMS9900 has native SRC (Shift Right Circular) for rotate right.
; Rotate left by N expands to rotate right by (16-N).
; LLVM recognizes (x << n) | (x >> (16-n)) patterns as rotates.

declare i16 @llvm.fshl.i16(i16, i16, i16)

; --- Rotate left by 4 via fshl intrinsic ---
; rotl(x, 4) expands to SRC x, 12 (rotate right by 16-4=12)
; CHECK-LABEL: rotl_4:
; CHECK: SRC{{[ \t]+}}R0,12
; CHECK: B{{[ \t]+}}*R11

define i16 @rotl_4(i16 %x) {
  %r = call i16 @llvm.fshl.i16(i16 %x, i16 %x, i16 4)
  ret i16 %r
}

; --- Rotate right by 4 via shift+or pattern ---
; LLVM recognizes this as rotr(x, 4) and emits SRC x, 4
; CHECK-LABEL: rotr_4:
; CHECK: SRC{{[ \t]+}}R0,4
; CHECK: B{{[ \t]+}}*R11

define i16 @rotr_4(i16 %x) {
  %r = lshr i16 %x, 4
  %l = shl i16 %x, 12
  %rot = or i16 %l, %r
  ret i16 %rot
}

; --- Rotate left by 5 via shift+or pattern ---
; LLVM recognizes this as rotl(x, 5), then expands to SRC x, 11
; CHECK-LABEL: rotl_pattern:
; CHECK: SRC{{[ \t]+}}R0,11
; CHECK: B{{[ \t]+}}*R11

define i16 @rotl_pattern(i16 %x) {
  %l = shl i16 %x, 5
  %r = lshr i16 %x, 11
  %rot = or i16 %l, %r
  ret i16 %rot
}
