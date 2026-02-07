; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s

; Test that global address + register offset folds into indexed addressing.
; This avoids a separate LI + A sequence.

target datalayout = "e-m:e-p:16:16-i16:16-a:0:16-n16"

@arr = external global [10 x i16]
@barr = external global [10 x i8]

; Word load with indexed addressing
; CHECK-LABEL: load_global_idx:
; CHECK: MOV @arr(R{{[1-9][0-5]?}}),R
; CHECK-NOT: LI {{.*}}arr
define i16 @load_global_idx(i16 %idx) {
  %ptr = getelementptr inbounds [10 x i16], ptr @arr, i16 0, i16 %idx
  %val = load i16, ptr %ptr
  ret i16 %val
}

; Word store with indexed addressing
; CHECK-LABEL: store_global_idx:
; CHECK: MOV R{{[0-9]+}},@arr(R{{[1-9][0-5]?}})
; CHECK-NOT: LI {{.*}}arr
define void @store_global_idx(i16 %idx, i16 %val) {
  %ptr = getelementptr inbounds [10 x i16], ptr @arr, i16 0, i16 %idx
  store i16 %val, ptr %ptr
  ret void
}

; Byte load with indexed addressing
; CHECK-LABEL: load_byte_idx:
; CHECK: MOVB @barr(R{{[1-9][0-5]?}}),R
; CHECK-NOT: LI {{.*}}barr
define i8 @load_byte_idx(i16 %idx) {
  %ptr = getelementptr inbounds [10 x i8], ptr @barr, i16 0, i16 %idx
  %val = load i8, ptr %ptr
  ret i8 %val
}

; Byte store with indexed addressing
; CHECK-LABEL: store_byte_idx:
; CHECK: MOVB R{{[0-9]+}},@barr(R{{[1-9][0-5]?}})
; CHECK-NOT: LI {{.*}}barr
define void @store_byte_idx(i16 %idx, i8 %val) {
  %ptr = getelementptr inbounds [10 x i8], ptr @barr, i16 0, i16 %idx
  store i8 %val, ptr %ptr
  ret void
}
