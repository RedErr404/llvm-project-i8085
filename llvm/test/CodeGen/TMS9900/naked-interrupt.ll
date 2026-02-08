; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test function attributes: naked and interrupt.
;
; Naked functions should have no prologue/epilogue.
; Interrupt functions should use RTWP (return from interrupt) instead of B *R11.

; --- Naked function: no prologue, no epilogue ---
; CHECK-LABEL: naked_func:
; CHECK-NOT: DECT{{[ \t]+}}R10
; CHECK-NOT: MOV{{[ \t]+}}R11
; CHECK: LI{{[ \t]+}}R0,42
; CHECK: B{{[ \t]+}}*R11
; CHECK-NOT: MOV{{[ \t]+}}*R10+,R11

define void @naked_func() naked {
  call void asm sideeffect "LI R0, 42\0AB *R11", ""()
  unreachable
}

; --- Interrupt handler: no prologue, returns with RTWP ---
; CHECK-LABEL: isr:
; CHECK-NOT: DECT{{[ \t]+}}R10
; CHECK-NOT: B{{[ \t]+}}*R11
; CHECK: RTWP

define void @isr() #0 {
  ret void
}

attributes #0 = { "interrupt" }
