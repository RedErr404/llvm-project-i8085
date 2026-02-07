; RUN: llc -march=tms9900 -O0 < %s | FileCheck %s

; CHECK-LABEL: load_i8
; CHECK: MOVB{{[ \t]+}}*R0,R0
; CHECK: SRL{{[ \t]+}}R0,8

define i8 @load_i8(i8* %p) {
entry:
  %v = load i8, i8* %p, align 1
  ret i8 %v
}

; CHECK-LABEL: store_i8
; CHECK: SWPB{{[ \t]+}}R1
; CHECK: MOVB{{[ \t]+}}R1,*R0

define void @store_i8(i8* %p, i8 %v) {
entry:
  store i8 %v, i8* %p, align 1
  ret void
}
