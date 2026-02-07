; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test select (conditional move) operations with various comparison types.
; The TMS9900 backend lowers select to a compare + conditional branch
; sequence that picks one of two values.

; --- select with eq ---
; CHECK-LABEL: sel_eq:
; CHECK: C{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: MOV{{[ \t]+}}R{{[0-9]+}},R0
; CHECK: B{{[ \t]+}}*R11

define i16 @sel_eq(i16 %a, i16 %b, i16 %x, i16 %y) {
  %cmp = icmp eq i16 %a, %b
  %sel = select i1 %cmp, i16 %x, i16 %y
  ret i16 %sel
}

; --- select with ne ---
; CHECK-LABEL: sel_ne:
; CHECK: C{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: MOV{{[ \t]+}}R{{[0-9]+}},R0
; CHECK: B{{[ \t]+}}*R11

define i16 @sel_ne(i16 %a, i16 %b, i16 %x, i16 %y) {
  %cmp = icmp ne i16 %a, %b
  %sel = select i1 %cmp, i16 %x, i16 %y
  ret i16 %sel
}

; --- select with signed greater than ---
; CHECK-LABEL: sel_sgt:
; CHECK: C{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: MOV{{[ \t]+}}R{{[0-9]+}},R0
; CHECK: B{{[ \t]+}}*R11

define i16 @sel_sgt(i16 %a, i16 %b, i16 %x, i16 %y) {
  %cmp = icmp sgt i16 %a, %b
  %sel = select i1 %cmp, i16 %x, i16 %y
  ret i16 %sel
}

; --- select with signed less than ---
; CHECK-LABEL: sel_slt:
; CHECK: C{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: MOV{{[ \t]+}}R{{[0-9]+}},R0
; CHECK: B{{[ \t]+}}*R11

define i16 @sel_slt(i16 %a, i16 %b, i16 %x, i16 %y) {
  %cmp = icmp slt i16 %a, %b
  %sel = select i1 %cmp, i16 %x, i16 %y
  ret i16 %sel
}

; --- select with unsigned greater than ---
; Uses JLE (logical low or equal) to branch on unsigned comparison.
; CHECK-LABEL: sel_ugt:
; CHECK: C{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: JLE
; CHECK: MOV{{[ \t]+}}R{{[0-9]+}},R0
; CHECK: B{{[ \t]+}}*R11

define i16 @sel_ugt(i16 %a, i16 %b, i16 %x, i16 %y) {
  %cmp = icmp ugt i16 %a, %b
  %sel = select i1 %cmp, i16 %x, i16 %y
  ret i16 %sel
}

; --- select with unsigned less than ---
; Uses JHE (logical high or equal) to branch on unsigned comparison.
; CHECK-LABEL: sel_ult:
; CHECK: C{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: JHE
; CHECK: MOV{{[ \t]+}}R{{[0-9]+}},R0
; CHECK: B{{[ \t]+}}*R11

define i16 @sel_ult(i16 %a, i16 %b, i16 %x, i16 %y) {
  %cmp = icmp ult i16 %a, %b
  %sel = select i1 %cmp, i16 %x, i16 %y
  ret i16 %sel
}

; --- select with constant true/false values ---
; CHECK-LABEL: sel_const:
; CHECK: C{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: B{{[ \t]+}}*R11

define i16 @sel_const(i16 %a, i16 %b) {
  %cmp = icmp eq i16 %a, %b
  %sel = select i1 %cmp, i16 1, i16 0
  ret i16 %sel
}
