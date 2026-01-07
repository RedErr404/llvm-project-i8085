; RUN: llc -march=tms9900 -O0 < %s | FileCheck %s

; CHECK-LABEL: shl3
; CHECK: SLA{{[ \t]+}}R0,3

define i16 @shl3(i16 %x) {
entry:
  %v = shl i16 %x, 3
  ret i16 %v
}

; CHECK-LABEL: ashr4
; CHECK: SRA{{[ \t]+}}R0,4

define i16 @ashr4(i16 %x) {
entry:
  %v = ashr i16 %x, 4
  ret i16 %v
}

; CHECK-LABEL: lshr5
; CHECK: SRL{{[ \t]+}}R0,5

define i16 @lshr5(i16 %x) {
entry:
  %v = lshr i16 %x, 5
  ret i16 %v
}
