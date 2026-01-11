; RUN: llc -march=tms9900 -O0 < %s | FileCheck %s

; CHECK-LABEL: test_eq
; CHECK: C{{[ \t]+}}R1,R0
; CHECK: JNE{{[ \t]+}}[[EQ_NO:LBB[0-9_]+]]

define i16 @test_eq(i16 %a, i16 %b) {
entry:
  %cmp = icmp eq i16 %a, %b
  br i1 %cmp, label %yes, label %no

yes:
  ret i16 1

no:
  ret i16 0
}

; CHECK-LABEL: test_ne
; CHECK: C{{[ \t]+}}R1,R0
; CHECK: JEQ{{[ \t]+}}[[NE_NO:LBB[0-9_]+]]

define i16 @test_ne(i16 %a, i16 %b) {
entry:
  %cmp = icmp ne i16 %a, %b
  br i1 %cmp, label %yes, label %no

yes:
  ret i16 1

no:
  ret i16 0
}

; CHECK-LABEL: test_ult
; CHECK: C{{[ \t]+}}R1,R0
; CHECK: JHE{{[ \t]+}}[[ULT_NO:LBB[0-9_]+]]

define i16 @test_ult(i16 %a, i16 %b) {
entry:
  %cmp = icmp ult i16 %a, %b
  br i1 %cmp, label %yes, label %no

yes:
  ret i16 1

no:
  ret i16 0
}

; CHECK-LABEL: test_ugt
; CHECK: C{{[ \t]+}}R1,R0
; CHECK: JLE{{[ \t]+}}[[UGT_NO:LBB[0-9_]+]]

define i16 @test_ugt(i16 %a, i16 %b) {
entry:
  %cmp = icmp ugt i16 %a, %b
  br i1 %cmp, label %yes, label %no

yes:
  ret i16 1

no:
  ret i16 0
}

; CHECK-LABEL: test_slt
; CHECK: C{{[ \t]+}}R1,R0
; CHECK: JEQ{{[ \t]+}}[[SLT_NO:LBB[0-9_]+]]
; CHECK: JGT{{[ \t]+}}[[SLT_NO]]

define i16 @test_slt(i16 %a, i16 %b) {
entry:
  %cmp = icmp slt i16 %a, %b
  br i1 %cmp, label %yes, label %no

yes:
  ret i16 1

no:
  ret i16 0
}

; CHECK-LABEL: test_sgt
; CHECK: C{{[ \t]+}}R1,R0
; CHECK: JEQ{{[ \t]+}}[[SGT_NO:LBB[0-9_]+]]
; CHECK: JLT{{[ \t]+}}[[SGT_NO]]

define i16 @test_sgt(i16 %a, i16 %b) {
entry:
  %cmp = icmp sgt i16 %a, %b
  br i1 %cmp, label %yes, label %no

yes:
  ret i16 1

no:
  ret i16 0
}

; CHECK-LABEL: test_sle
; CHECK: C{{[ \t]+}}R1,R0
; CHECK: JGT{{[ \t]+}}[[SLE_NO:LBB[0-9_]+]]

define i16 @test_sle(i16 %a, i16 %b) {
entry:
  %cmp = icmp sle i16 %a, %b
  br i1 %cmp, label %yes, label %no

yes:
  ret i16 1

no:
  ret i16 0
}

; CHECK-LABEL: test_sge
; CHECK: C{{[ \t]+}}R1,R0
; CHECK: JLT{{[ \t]+}}[[SGE_NO:LBB[0-9_]+]]

define i16 @test_sge(i16 %a, i16 %b) {
entry:
  %cmp = icmp sge i16 %a, %b
  br i1 %cmp, label %yes, label %no

yes:
  ret i16 1

no:
  ret i16 0
}
