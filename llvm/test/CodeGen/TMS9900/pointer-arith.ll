; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test pointer arithmetic on the 16-bit TMS9900.
; Pointers are 16 bits wide. GEP computations should fold into
; indexed addressing where possible.

@arr = external global [100 x i16]

; --- Pointer increment ---
; CHECK-LABEL: ptr_inc:
; CHECK: INCT{{[ \t]+}}R0
; CHECK: B{{[ \t]+}}*R11

define ptr @ptr_inc(ptr %p) {
  %next = getelementptr i16, ptr %p, i16 1
  ret ptr %next
}

; --- Pointer from array + index uses indexed addressing ---
; CHECK-LABEL: array_element:
; CHECK: @arr(R{{[0-9]+}})
; CHECK: B{{[ \t]+}}*R11

define i16 @array_element(i16 %idx) {
  %p = getelementptr [100 x i16], ptr @arr, i16 0, i16 %idx
  %v = load i16, ptr %p
  ret i16 %v
}

; --- Pointer difference (subtraction) ---
; CHECK-LABEL: ptr_diff:
; CHECK: S{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: B{{[ \t]+}}*R11

define i16 @ptr_diff(ptr %a, ptr %b) {
  %ai = ptrtoint ptr %a to i16
  %bi = ptrtoint ptr %b to i16
  %d = sub i16 %ai, %bi
  ret i16 %d
}
