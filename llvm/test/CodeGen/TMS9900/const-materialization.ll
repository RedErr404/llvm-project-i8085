; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test constant materialization:
;   - 0 -> CLR (via peephole LI 0 -> CLR)
;   - -1/0xFFFF -> SETO (via peephole LI -1 -> SETO)
;   - Other values -> LI

; CHECK-LABEL: const_zero:
; CHECK: CLR{{[ \t]+}}R0
; CHECK: B{{[ \t]+}}*R11

define i16 @const_zero() {
  ret i16 0
}

; CHECK-LABEL: const_neg1:
; CHECK: SETO{{[ \t]+}}R0
; CHECK: B{{[ \t]+}}*R11

define i16 @const_neg1() {
  ret i16 -1
}

; CHECK-LABEL: const_1:
; CHECK: LI{{[ \t]+}}R0,1
; CHECK: B{{[ \t]+}}*R11

define i16 @const_1() {
  ret i16 1
}

; CHECK-LABEL: const_large:
; CHECK: LI{{[ \t]+}}R0,12345
; CHECK: B{{[ \t]+}}*R11

define i16 @const_large() {
  ret i16 12345
}
