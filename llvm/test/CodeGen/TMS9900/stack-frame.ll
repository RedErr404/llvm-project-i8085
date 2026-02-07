; RUN: llc -march=tms9900 -O0 < %s | FileCheck %s
;
; Test stack frame management:
;   - R10 is the stack pointer
;   - Stack grows downward (AI R10,-N to allocate, AI R10,N to deallocate)
;   - Local variables accessed via @offset(R10)

; --- Simple stack alloca ---
; CHECK-LABEL: stack_alloca:
; CHECK: AI{{[ \t]+}}R10,-4
; CHECK: AI{{[ \t]+}}R10,4
; CHECK: B{{[ \t]+}}*R11

define i16 @stack_alloca() {
  %a = alloca i16, align 2
  %b = alloca i16, align 2
  store i16 42, ptr %a
  store i16 99, ptr %b
  %va = load i16, ptr %a
  %vb = load i16, ptr %b
  %sum = add i16 %va, %vb
  ret i16 %sum
}

; --- Stack spill in non-leaf function ---
; When making a call, R11 is spilled to the stack.
; CHECK-LABEL: stack_spill:
; CHECK: DECT{{[ \t]+}}R10
; CHECK: MOV{{[ \t]+}}R11,*R10
; CHECK: BL{{[ \t]+}}@sink
; CHECK: MOV{{[ \t]+}}*R10+,R11
; CHECK: B{{[ \t]+}}*R11

declare void @sink(i16)

define void @stack_spill(i16 %a) {
  call void @sink(i16 %a)
  ret void
}
