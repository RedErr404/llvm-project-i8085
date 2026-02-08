; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test blockaddress and indirectbr (computed goto).
; The backend should load a target address from a table and branch
; indirectly via B *Rx.

; CHECK-LABEL: test_indirectbr:
; CHECK:       SLA {{R[0-9]+}},1
; CHECK:       MOV @targets({{R[0-9]+}}),{{R[0-9]+}}
; CHECK:       B *{{R[0-9]+}}

@targets = constant [2 x ptr] [ptr blockaddress(@test_indirectbr, %bb1), ptr blockaddress(@test_indirectbr, %bb2)]

define i16 @test_indirectbr(i16 %idx) {
entry:
  %ptr = getelementptr [2 x ptr], ptr @targets, i16 0, i16 %idx
  %addr = load ptr, ptr %ptr
  indirectbr ptr %addr, [label %bb1, label %bb2]
bb1:
  ret i16 1
bb2:
  ret i16 2
}
