; RUN: llc -march=tms9900 -O2 < %s | FileCheck %s
;
; Test min/max intrinsics.
; These expand to compare + conditional branch (select) patterns.
; Signed versions use signed comparison branches (JGT/JLT).
; Unsigned versions use unsigned comparison branches (JHE/JLE/JH/JL).

declare i16 @llvm.smin.i16(i16, i16)
declare i16 @llvm.smax.i16(i16, i16)
declare i16 @llvm.umin.i16(i16, i16)
declare i16 @llvm.umax.i16(i16, i16)

; --- Signed minimum ---
; Compare then conditionally select the smaller value.
; Uses signed comparison (C instruction + JGT for signed greater-than).
; CHECK-LABEL: smin_val:
; CHECK: C{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: B{{[ \t]+}}*R11

define i16 @smin_val(i16 %a, i16 %b) {
  %r = call i16 @llvm.smin.i16(i16 %a, i16 %b)
  ret i16 %r
}

; --- Signed maximum ---
; Compare then conditionally select the larger value.
; CHECK-LABEL: smax_val:
; CHECK: C{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: B{{[ \t]+}}*R11

define i16 @smax_val(i16 %a, i16 %b) {
  %r = call i16 @llvm.smax.i16(i16 %a, i16 %b)
  ret i16 %r
}

; --- Unsigned minimum ---
; Uses unsigned comparison branch (JHE = jump if logically high or equal).
; CHECK-LABEL: umin_val:
; CHECK: C{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: JHE
; CHECK: B{{[ \t]+}}*R11

define i16 @umin_val(i16 %a, i16 %b) {
  %r = call i16 @llvm.umin.i16(i16 %a, i16 %b)
  ret i16 %r
}

; --- Unsigned maximum ---
; Uses unsigned comparison branch (JLE = jump if logically low or equal).
; CHECK-LABEL: umax_val:
; CHECK: C{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: JLE
; CHECK: B{{[ \t]+}}*R11

define i16 @umax_val(i16 %a, i16 %b) {
  %r = call i16 @llvm.umax.i16(i16 %a, i16 %b)
  ret i16 %r
}
