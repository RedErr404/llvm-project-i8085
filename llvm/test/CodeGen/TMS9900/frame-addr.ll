; RUN: llc -march=tms9900 -O0 < %s | FileCheck %s

target triple = "tms9900"

declare void @sink(i16*)

; CHECK-LABEL: test_lea
; CHECK: MOV{{[ \t]+}}R10,R0
; CHECK: AI{{[ \t]+}}R0,6
; CHECK: BL{{[ \t]+}}@sink
define void @test_lea() {
entry:
  %arr = alloca [4 x i16], align 2
  %p = getelementptr inbounds [4 x i16], ptr %arr, i16 0, i16 3
  call void @sink(i16* %p)
  ret void
}
