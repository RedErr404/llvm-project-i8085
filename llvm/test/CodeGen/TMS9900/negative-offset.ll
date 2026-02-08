; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test negative indexed addressing offsets.
; The TMS9900 supports negative constant offsets in indexed addressing:
;   MOV @-2(Rx), Ry    ; load from Rx - 2
;   MOV Ry, @-4(Rx)    ; store to Rx - 4

target datalayout = "e-m:e-p:16:16-i16:16-a:0:16-n16"

; --- Load with negative offset -2 (one i16 element back) ---
; CHECK-LABEL: neg_offset:
; CHECK: MOV{{[ \t]+}}@-2(R{{[0-9]+}}),R0
; CHECK: B{{[ \t]+}}*R11

define i16 @neg_offset(ptr %p) {
  %q = getelementptr i16, ptr %p, i16 -1
  %v = load i16, ptr %q
  ret i16 %v
}

; --- Load with negative offset -8 (four i16 elements back) ---
; CHECK-LABEL: neg_offset_4:
; CHECK: MOV{{[ \t]+}}@-8(R{{[0-9]+}}),R0
; CHECK: B{{[ \t]+}}*R11

define i16 @neg_offset_4(ptr %p) {
  %q = getelementptr i16, ptr %p, i16 -4
  %v = load i16, ptr %q
  ret i16 %v
}

; --- Store with negative offset -4 (two i16 elements back) ---
; CHECK-LABEL: neg_offset_store:
; CHECK: MOV{{[ \t]+}}R{{[0-9]+}},@-4(R{{[0-9]+}})
; CHECK: B{{[ \t]+}}*R11

define void @neg_offset_store(ptr %p, i16 %val) {
  %q = getelementptr i16, ptr %p, i16 -2
  store i16 %val, ptr %q
  ret void
}
