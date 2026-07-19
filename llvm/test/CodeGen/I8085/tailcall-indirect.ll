; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s

; A tail call through a function pointer must not crash the backend. It lowers
; to a PCHL jump (no return address pushed): the target is carried in BC across
; the epilogue (which clobbers HL), then moved into HL and jumped to.

define void @itail_void(ptr %f) {
; CHECK-LABEL: itail_void:
; CHECK:      PCHL
; CHECK-NOT:  CALL
  tail call void %f()
  ret void
}

define i8 @itail_arg(ptr %f, i8 %x) {
; CHECK-LABEL: itail_arg:
; CHECK:      PCHL
  %r = tail call i8 %f(i8 %x)
  ret i8 %r
}

; NON-tail indirect call (result used, so control returns): must keep working
; via the CALL_INDIRECT trampoline - push a return address, PCHL to the target,
; then continue after it returns.
define i8 @icall_nontail(ptr %f, i8 %x) {
; CHECK-LABEL: icall_nontail:
; CHECK:      PUSH B
; CHECK-NEXT: PCHL
; CHECK:      RET
  %r = call i8 %f(i8 %x)
  %s = add i8 %r, 1
  ret i8 %s
}
