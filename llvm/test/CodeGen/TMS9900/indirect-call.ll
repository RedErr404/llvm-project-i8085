; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test indirect function calls (calling through function pointers).
; The TMS9900 uses BL *Rx for indirect calls.

; --- Simple indirect call with no arguments ---
; The function pointer is in R0 on entry. Expect BL *Rx for indirect call.
;
; CHECK-LABEL: indirect_call:
; CHECK:       DECT R10
; CHECK:       MOV R11,*R10
; CHECK:       BL *R0
; CHECK:       MOV *R10+,R11
; CHECK:       B *R11

define void @indirect_call(ptr %fptr) {
  call void %fptr()
  ret void
}

; --- Indirect call with arguments and return value ---
; Function pointer in R0, args in R1/R2. The pointer must be moved to
; another register so R0/R1 can hold the arguments for the callee.
;
; CHECK-LABEL: indirect_call_args:
; CHECK:       DECT R10
; CHECK:       MOV R11,*R10
; CHECK:       BL *{{R[0-9]+}}
; CHECK:       MOV *R10+,R11
; CHECK:       B *R11

define i16 @indirect_call_args(ptr %fptr, i16 %a, i16 %b) {
  %r = call i16 %fptr(i16 %a, i16 %b)
  ret i16 %r
}
