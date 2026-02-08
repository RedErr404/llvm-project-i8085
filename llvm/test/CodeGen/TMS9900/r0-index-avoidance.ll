; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test that R0 is never used as an index register.
; On the TMS9900, @offset(R0) does NOT work as indexed addressing -
; it instead encodes as symbolic/absolute addressing @addr.
; The register allocator must avoid placing index values in R0.
; The IdxRegs register class should exclude R0.

target datalayout = "e-m:e-p:16:16-i16:16-a:0:16-n16"

@table = global [4 x i16] [i16 10, i16 20, i16 30, i16 40]

; --- Array lookup: index must not be in R0 ---
; The index is shifted left by 1 (SLA for word offset) then used
; in indexed addressing @table(Rx) where Rx != R0.
; CHECK-LABEL: lookup:
; CHECK: SLA{{[ \t]+}}R{{[1-9][0-5]?}},1
; CHECK: MOV{{[ \t]+}}@table(R{{[1-9][0-5]?}}),R0
; CHECK-NOT: @table(R0)
; CHECK: B{{[ \t]+}}*R11

define i16 @lookup(i16 %idx) {
  %ptr = getelementptr [4 x i16], ptr @table, i16 0, i16 %idx
  %v = load i16, ptr %ptr
  ret i16 %v
}

; --- Array store: index must not be in R0 ---
; CHECK-LABEL: store_lookup:
; CHECK: SLA{{[ \t]+}}R{{[1-9][0-5]?}},1
; CHECK: MOV{{[ \t]+}}R{{[0-9]+}},@table(R{{[1-9][0-5]?}})
; CHECK-NOT: @table(R0)
; CHECK: B{{[ \t]+}}*R11

define void @store_lookup(i16 %idx, i16 %val) {
  %ptr = getelementptr [4 x i16], ptr @table, i16 0, i16 %idx
  store i16 %val, ptr %ptr
  ret void
}
