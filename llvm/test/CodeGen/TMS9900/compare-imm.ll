; RUN: llc -mtriple=tms9900 -O2 -disable-branch-fold -disable-block-placement < %s | FileCheck %s
;
; Immediate compares should use CI with the correct branch condition
; even when the immediate is on the LHS (operand swap).

define i16 @slt_imm_rhs(i16 %a) {
entry:
  %cmp = icmp slt i16 %a, 5
  br i1 %cmp, label %yes, label %no

yes:
  ret i16 1

no:
  ret i16 0
}

; CHECK-LABEL: slt_imm_rhs
; CHECK: CI{{[ \t]+}}R{{[0-9]+}},{{-?[0-9]+}}
; CHECK-NEXT: JGT{{[ \t]+}}[[NO:LBB[0-9_]+]]


define i16 @slt_imm_lhs(i16 %a) {
entry:
  %cmp = icmp slt i16 5, %a
  br i1 %cmp, label %yes, label %no

yes:
  ret i16 1

no:
  ret i16 0
}

; CHECK-LABEL: slt_imm_lhs
; CHECK: CI{{[ \t]+}}R{{[0-9]+}},{{-?[0-9]+}}
; CHECK-NEXT: JLT{{[ \t]+}}[[NO:LBB[0-9_]+]]
