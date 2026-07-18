; RUN: llc -mattr=i8085,sram < %s -march=i8085 | FileCheck %s

; Dense and sparse switch lowering.
; Adapted from RISC-V switch patterns.

; Dense switch with 5 contiguous cases
define i16 @switch_dense_5(i16 %x) {
; CHECK-LABEL: switch_dense_5:
; CHECK-DAG: LXI B, 100
; CHECK-DAG: LXI B, 200
; CHECK-DAG: LXI B, 300
; CHECK-DAG: LXI B, 400
; CHECK-DAG: LXI B, 500
; CHECK-DAG: LXI B, 0
entry:
  switch i16 %x, label %default [
    i16 0, label %case0
    i16 1, label %case1
    i16 2, label %case2
    i16 3, label %case3
    i16 4, label %case4
  ]

case0:
  ret i16 100
case1:
  ret i16 200
case2:
  ret i16 300
case3:
  ret i16 400
case4:
  ret i16 500
default:
  ret i16 0
}

; Sparse switch with large gaps between cases
define i8 @switch_sparse(i8 %x) {
; CHECK-LABEL: switch_sparse:
; CHECK-DAG: MVI A, 10
; CHECK-DAG: MVI A, 20
; CHECK-DAG: MVI A, 30
; CHECK-DAG: MVI A, 40
; The default `return 0` zeroes A via XRA A (MVI A,0 -> XRA A where flags dead).
; CHECK-DAG: XRA A
entry:
  switch i8 %x, label %default [
    i8 1, label %case1
    i8 50, label %case50
    i8 100, label %case100
    i8 200, label %case200
  ]

case1:
  ret i8 10
case50:
  ret i8 20
case100:
  ret i8 30
case200:
  ret i8 40
default:
  ret i8 0
}

; Two-case switch (should reduce to compare+branch)
define i16 @switch_two_cases(i16 %x) {
; CHECK-LABEL: switch_two_cases:
; CHECK-DAG: LXI B, 42
; CHECK-DAG: LXI B, 84
; CHECK-DAG: LXI B, 0
entry:
  switch i16 %x, label %default [
    i16 0, label %case0
    i16 1, label %case1
  ]

case0:
  ret i16 42
case1:
  ret i16 84
default:
  ret i16 0
}
