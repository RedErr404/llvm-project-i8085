; RUN: llc -march=tms9900 -O0 -stop-after=finalize-isel < %s | FileCheck %s

target triple = "tms9900"

; CHECK-LABEL: name: test_eq
; CHECK: CMPBRrr
; CHECK: JMP
define i16 @test_eq(i16 %a, i16 %b) {
entry:
  %cmp = icmp eq i16 %a, %b
  br i1 %cmp, label %yes, label %no

yes:
  ret i16 1

no:
  ret i16 0
}
