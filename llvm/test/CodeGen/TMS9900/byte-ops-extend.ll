; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s

; CHECK-LABEL: zext_load_i8
; CHECK: MOVB{{[ \t]+}}*R{{[0-9]+}},R{{[0-9]+}}
; CHECK-NEXT: SRL{{[ \t]+}}R{{[0-9]+}},8

define i16 @zext_load_i8(i8* %p) {
entry:
  %v = load i8, i8* %p, align 1
  %z = zext i8 %v to i16
  ret i16 %z
}

; CHECK-LABEL: sext_load_i8
; CHECK: MOVB{{[ \t]+}}*R{{[0-9]+}},R{{[0-9]+}}
; CHECK-NEXT: SRA{{[ \t]+}}R{{[0-9]+}},8

define i16 @sext_load_i8(i8* %p) {
entry:
  %v = load i8, i8* %p, align 1
  %s = sext i8 %v to i16
  ret i16 %s
}
