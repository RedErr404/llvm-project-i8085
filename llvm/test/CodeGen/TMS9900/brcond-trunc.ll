; RUN: llc -mtriple=tms9900 -O2 -disable-branch-fold -disable-block-placement < %s | FileCheck %s

; The ANDI sets flags on the result, so the CI Rx,0 is eliminated by peephole.
; CHECK-LABEL: brcond_trunc
; CHECK: ANDI{{[ \t]+}}R{{[0-9]+}},1
; CHECK-NEXT: JEQ{{[ \t]+}}[[NO:LBB[0-9_]+]]

define void @brcond_trunc(i16 %a) {
entry:
  %b = trunc i16 %a to i1
  br i1 %b, label %yes, label %no

yes:
  ret void

no:
  ret void
}
