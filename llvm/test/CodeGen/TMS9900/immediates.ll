; RUN: llc -march=tms9900 -O0 < %s | FileCheck %s

; CHECK-LABEL: const_imm
; CHECK: LI{{[ \t]+}}R0,4660

define i16 @const_imm() {
entry:
  ret i16 4660
}

; CHECK-LABEL: add_imm
; CHECK: AI{{[ \t]+}}R0,42

define i16 @add_imm(i16 %x) {
entry:
  %add = add i16 %x, 42
  ret i16 %add
}

; CHECK-LABEL: and_imm
; CHECK: ANDI{{[ \t]+}}R0,255

define i16 @and_imm(i16 %x) {
entry:
  %v = and i16 %x, 255
  ret i16 %v
}

; CHECK-LABEL: or_imm
; CHECK: ORI{{[ \t]+}}R0,3855

define i16 @or_imm(i16 %x) {
entry:
  %v = or i16 %x, 3855
  ret i16 %v
}

; CHECK-LABEL: cmp_imm
; CHECK: CI{{[ \t]+}}R0,8738
; CHECK: JNE{{[ \t]+}}[[CMP_NO:LBB[0-9_]+]]

define i16 @cmp_imm(i16 %x) {
entry:
  %cmp = icmp eq i16 %x, 8738
  br i1 %cmp, label %yes, label %no

yes:
  ret i16 1

no:
  ret i16 0
}
