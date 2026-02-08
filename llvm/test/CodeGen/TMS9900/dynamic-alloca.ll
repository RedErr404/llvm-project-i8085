; RUN: llc -march=tms9900 -O2 < %s | FileCheck %s
;
; Test dynamic stack allocation (variable-length arrays).
; DYNAMIC_STACKALLOC is Expand on TMS9900.
; The expansion computes the byte size (n * element_size), then
; subtracts from the stack pointer (R10) to allocate space.

; --- Variable-length allocation and store ---
; Allocates n * 2 bytes on the stack (i16 elements), stores a value.
; SLA R0,1 computes byte count, then adjusts R10 (stack pointer).
; CHECK-LABEL: vla:
; CHECK: SLA{{[ \t]+}}R0,1
; CHECK: S{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: MOV{{[ \t]+}}R{{[0-9]+}},R10
; CHECK: B{{[ \t]+}}*R11

define void @vla(i16 %n) {
  %p = alloca i16, i16 %n
  store i16 42, ptr %p
  ret void
}

; --- Variable-length allocation with load back ---
; Allocates, stores, then loads back from the dynamically allocated area.
; CHECK-LABEL: dynalloca_read:
; CHECK: SLA{{[ \t]+}}R0,1
; CHECK: S{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: MOV{{[ \t]+}}R{{[0-9]+}},R10
; CHECK: B{{[ \t]+}}*R11

define i16 @dynalloca_read(i16 %n) {
  %p = alloca i16, i16 %n
  store i16 100, ptr %p
  %v = load i16, ptr %p
  ret i16 %v
}
