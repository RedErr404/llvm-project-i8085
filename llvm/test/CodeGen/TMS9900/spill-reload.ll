; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test register spill and reload patterns under register pressure.
; The TMS9900 uses R10 as the stack pointer. When register pressure
; exceeds available registers, values are spilled to the stack via
; MOV Rx, @offset(R10) and reloaded via MOV @offset(R10), Rx.
; Callee-saved registers R13-R15 are also saved/restored.

target datalayout = "e-m:e-p:16:16-i16:16-a:0:16-n16"

declare i16 @use(i16)

; --- High register pressure forces spills ---
; With many live values across a call, the compiler must spill to stack.
; CHECK-LABEL: pressure:
; CHECK: DECT{{[ \t]+}}R10
; CHECK: MOV{{[ \t]+}}R11,*R10
;
; Callee-saved registers are saved to stack:
; CHECK: MOV{{[ \t]+}}R13,@{{[0-9]+}}(R10)
; CHECK: MOV{{[ \t]+}}R14,@{{[0-9]+}}(R10)
; CHECK: MOV{{[ \t]+}}R15,@{{[0-9]+}}(R10)
;
; Values spilled to stack via indexed addressing on R10:
; CHECK: MOV{{[ \t]+}}R{{[0-9]+}},@{{[0-9]+}}(R10)
;
; Call site:
; CHECK: BL{{[ \t]+}}@use
;
; Values reloaded from stack:
; CHECK: MOV{{[ \t]+}}@{{[0-9]+}}(R10),R{{[0-9]+}}
;
; Callee-saved registers restored:
; CHECK: MOV{{[ \t]+}}@{{[0-9]+}}(R10),R15
; CHECK: MOV{{[ \t]+}}@{{[0-9]+}}(R10),R14
; CHECK: MOV{{[ \t]+}}@{{[0-9]+}}(R10),R13
;
; Stack frame deallocated and R11 restored:
; CHECK: AI{{[ \t]+}}R10,14
; CHECK-NEXT: MOV{{[ \t]+}}*R10+,R11
; CHECK-NEXT: B{{[ \t]+}}*R11

define i16 @pressure(i16 %a, i16 %b, i16 %c, i16 %d) {
  %v1 = add i16 %a, %b
  %v2 = add i16 %c, %d
  %v3 = mul i16 %a, %c
  %v4 = mul i16 %b, %d
  %v5 = mul i16 %v1, %v2
  %v6 = mul i16 %v3, %v4
  %call1 = call i16 @use(i16 %v5)
  %r1 = add i16 %v1, %v3
  %r2 = add i16 %v2, %v4
  %r3 = add i16 %v5, %v6
  %r4 = add i16 %call1, %r1
  %r = add i16 %r4, %r2
  %final = add i16 %r, %r3
  ret i16 %final
}
