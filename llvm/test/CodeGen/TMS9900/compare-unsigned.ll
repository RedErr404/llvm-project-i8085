; RUN: llc -mtriple=tms9900 -O2 -disable-branch-fold -disable-block-placement < %s | FileCheck %s
;
; Test unsigned comparison codegen with immediate operands.
; The TMS9900 uses CI for immediate compares and unsigned branch
; conditions: JH (logical high), JHE (high or equal), JL (logical low),
; JLE (low or equal).

; --- unsigned less than immediate ---
; CHECK-LABEL: ult_imm:
; CHECK: CI{{[ \t]+}}R{{[0-9]+}},{{[0-9]+}}
; CHECK: JH

define i16 @ult_imm(i16 %a) {
entry:
  %cmp = icmp ult i16 %a, 100
  br i1 %cmp, label %yes, label %no

yes:
  ret i16 1

no:
  ret i16 0
}

; --- unsigned greater than immediate ---
; CHECK-LABEL: ugt_imm:
; CHECK: CI{{[ \t]+}}R{{[0-9]+}},{{[0-9]+}}
; CHECK: JL

define i16 @ugt_imm(i16 %a) {
entry:
  %cmp = icmp ugt i16 %a, 100
  br i1 %cmp, label %yes, label %no

yes:
  ret i16 1

no:
  ret i16 0
}

; --- unsigned less than or equal ---
; CHECK-LABEL: ule_imm:
; CHECK: CI{{[ \t]+}}R{{[0-9]+}},{{[0-9]+}}

define i16 @ule_imm(i16 %a) {
entry:
  %cmp = icmp ule i16 %a, 100
  br i1 %cmp, label %yes, label %no

yes:
  ret i16 1

no:
  ret i16 0
}

; --- unsigned greater than or equal ---
; CHECK-LABEL: uge_imm:
; CHECK: CI{{[ \t]+}}R{{[0-9]+}},{{[0-9]+}}

define i16 @uge_imm(i16 %a) {
entry:
  %cmp = icmp uge i16 %a, 100
  br i1 %cmp, label %yes, label %no

yes:
  ret i16 1

no:
  ret i16 0
}
