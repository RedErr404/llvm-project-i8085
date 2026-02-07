; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test that callee-saved registers (R13, R14, R15) are preserved across calls.
; Per the TMS9900 calling convention:
;   - R0-R9, R12 are scratch (caller-saved)
;   - R10 (SP), R11 (LR), R13, R14, R15 are callee-saved

declare i16 @external(i16)

; A function that needs to preserve a value across a call should save a
; callee-saved register (R13 is first callee-saved available for allocation).
; CHECK-LABEL: preserve_across_call:
; CHECK: MOV{{[ \t]+}}R13,@{{[0-9]+}}(R10)
; CHECK: BL{{[ \t]+}}@external
; CHECK: MOV{{[ \t]+}}@{{[0-9]+}}(R10),R13
; CHECK: B{{[ \t]+}}*R11

define i16 @preserve_across_call(i16 %a) {
entry:
  %r1 = call i16 @external(i16 %a)
  %r2 = call i16 @external(i16 %r1)
  %sum = add i16 %r1, %r2
  ret i16 %sum
}
