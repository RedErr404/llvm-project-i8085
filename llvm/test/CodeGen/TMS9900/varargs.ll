; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test variable argument function handling.
; va_start should set up the argument pointer to read variadic args
; from the stack. va_arg loads the next argument and advances the pointer.

target datalayout = "e-m:e-p:16:16-i16:16-a:0:16-n16"

declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)

; --- Simple varargs function: read first variadic arg ---
; The function allocates stack space for the va_list, calls va_start,
; reads one i16 argument, then returns it.
; CHECK-LABEL: va_func:
; CHECK: DECT{{[ \t]+}}R10
; CHECK: INCT
; CHECK: B{{[ \t]+}}*R11

define i16 @va_func(i16 %n, ...) {
  %ap = alloca ptr
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, i16
  call void @llvm.va_end(ptr %ap)
  ret i16 %v
}
