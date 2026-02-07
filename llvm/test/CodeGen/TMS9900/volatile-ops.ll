; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test volatile load and store operations.
; Volatile operations must not be reordered or eliminated by the optimizer.

; --- Volatile store ---
; CHECK-LABEL: store_volatile:
; CHECK: MOV{{[ \t]+}}R1,*R0
; CHECK: B{{[ \t]+}}*R11

define void @store_volatile(ptr %p, i16 %val) {
  store volatile i16 %val, ptr %p
  ret void
}

; --- Volatile load ---
; CHECK-LABEL: load_volatile:
; CHECK: MOV{{[ \t]+}}*R0,R0
; CHECK: B{{[ \t]+}}*R11

define i16 @load_volatile(ptr %p) {
  %v = load volatile i16, ptr %p
  ret i16 %v
}

; --- Multiple volatile stores should not be reordered ---
; CHECK-LABEL: multi_volatile_store:
; CHECK: MOV{{[ \t]+}}R1,*R0
; CHECK: MOV{{[ \t]+}}R1,*R0
; CHECK: B{{[ \t]+}}*R11

define void @multi_volatile_store(ptr %p, i16 %val) {
  store volatile i16 %val, ptr %p
  store volatile i16 %val, ptr %p
  ret void
}
