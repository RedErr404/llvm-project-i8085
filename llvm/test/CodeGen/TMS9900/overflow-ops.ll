; RUN: llc -march=tms9900 -O2 < %s | FileCheck %s
;
; Test overflow detection intrinsics.
; UADDO/SADDO are Expand on TMS9900, so LLVM generates inline add + comparison
; sequences to detect overflow.

declare {i16, i1} @llvm.uadd.with.overflow.i16(i16, i16)
declare {i16, i1} @llvm.sadd.with.overflow.i16(i16, i16)

; --- Unsigned add with overflow detection ---
; Unsigned overflow is detected by checking if the result is less than
; an operand (wrapping). Uses A (add) followed by C (compare).
; CHECK-LABEL: uadd_ovf:
; CHECK: A{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: C{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: B{{[ \t]+}}*R11

define {i16, i1} @uadd_ovf(i16 %a, i16 %b) {
  %r = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %a, i16 %b)
  ret {i16, i1} %r
}

; --- Signed add with overflow detection ---
; Signed overflow occurs when adding two values of the same sign produces
; a result of the opposite sign. Expands to add + sign comparison logic.
; CHECK-LABEL: sadd_ovf:
; CHECK: A{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: XOR{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: B{{[ \t]+}}*R11

define {i16, i1} @sadd_ovf(i16 %a, i16 %b) {
  %r = call {i16, i1} @llvm.sadd.with.overflow.i16(i16 %a, i16 %b)
  ret {i16, i1} %r
}
