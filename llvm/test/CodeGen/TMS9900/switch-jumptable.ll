; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test switch statement lowering: jump tables, if/else chains, and
; comparison chains for sparse cases.

; --- Jump table for contiguous dense switch ---
; A switch with 4 contiguous cases (0..3) should lower to a jump table:
; range check, shift index by 1 (word size), load from table, branch indirect.
;
; CHECK-LABEL: switch_jumptable:
; CHECK:       CI {{R[0-9]+}},3
; CHECK:       JH
; CHECK:       SLA {{R[0-9]+}},1
; CHECK:       MOV @JTI0_0({{R[0-9]+}}),{{R[0-9]+}}
; CHECK:       B *{{R[0-9]+}}

define i16 @switch_jumptable(i16 %x) {
entry:
  switch i16 %x, label %default [
    i16 0, label %case0
    i16 1, label %case1
    i16 2, label %case2
    i16 3, label %case3
  ]
case0:
  ret i16 10
case1:
  ret i16 20
case2:
  ret i16 30
case3:
  ret i16 40
default:
  ret i16 0
}

; --- Small switch (2 cases) should use if/else chain, not jump table ---
; Two cases are too few for a jump table; expect comparison-based lowering.
;
; CHECK-LABEL: switch_small:
; CHECK:       CI R0,1
; CHECK:       JEQ
; CHECK-NOT:   JTI

define i16 @switch_small(i16 %x) {
entry:
  switch i16 %x, label %default [
    i16 0, label %case0
    i16 1, label %case1
  ]
case0:
  ret i16 10
case1:
  ret i16 20
default:
  ret i16 0
}

; --- Non-contiguous (sparse) switch should generate comparison chain ---
; Cases 0, 5, 10, 100 are too sparse for a jump table. The backend should
; emit a series of CI + conditional branch instructions.
;
; CHECK-LABEL: switch_sparse:
; CHECK-DAG:   CI R0,5
; CHECK-DAG:   CI R0,10
; CHECK-DAG:   CI R0,100

define i16 @switch_sparse(i16 %x) {
entry:
  switch i16 %x, label %default [
    i16 0, label %case0
    i16 5, label %case5
    i16 10, label %case10
    i16 100, label %case100
  ]
case0:
  ret i16 1
case5:
  ret i16 2
case10:
  ret i16 3
case100:
  ret i16 4
default:
  ret i16 0
}
