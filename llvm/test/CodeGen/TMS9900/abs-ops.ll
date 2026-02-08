; RUN: llc -march=tms9900 -O2 < %s | FileCheck %s
;
; Test absolute value operation.
; Although TMS9900 has a native ABS instruction, LLVM expands llvm.abs
; to a branchless sequence: sign = x >> 15; (x ^ sign) - sign.
; This is equivalent and avoids a branch.

declare i16 @llvm.abs.i16(i16, i1)

; --- Absolute value (may be INT_MIN) ---
; Branchless expansion: shift right arithmetic by 15 to get sign mask,
; XOR with sign mask, subtract sign mask.
; CHECK-LABEL: abs_val:
; CHECK: SRA{{[ \t]+}}R{{[0-9]+}},15
; CHECK: XOR{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: S{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: B{{[ \t]+}}*R11

define i16 @abs_val(i16 %x) {
  %r = call i16 @llvm.abs.i16(i16 %x, i1 false)
  ret i16 %r
}

; --- Absolute value (INT_MIN is poison / nsw) ---
; Same branchless sequence even with nsw flag.
; CHECK-LABEL: abs_val_nsw:
; CHECK: SRA{{[ \t]+}}R{{[0-9]+}},15
; CHECK: XOR{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: S{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: B{{[ \t]+}}*R11

define i16 @abs_val_nsw(i16 %x) {
  %r = call i16 @llvm.abs.i16(i16 %x, i1 true)
  ret i16 %r
}
