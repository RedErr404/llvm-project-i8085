; RUN: llc -march=tms9900 -O0 < %s | FileCheck %s

declare i16 @callee(i16)

; CHECK-LABEL: caller
; CHECK: DECT{{[ \t]+}}R10
; CHECK: MOV{{[ \t]+}}R11,*R10
; CHECK: BL{{[ \t]+}}@callee
; CHECK: MOV{{[ \t]+}}*R10+,R11
; CHECK: B{{[ \t]+}}*R11

define i16 @caller(i16 %x) {
entry:
  %y = call i16 @callee(i16 %x)
  ret i16 %y
}
