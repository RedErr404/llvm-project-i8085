; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s

; Branch lowering should emit conditional jumps without leaving compare pseudos.

define i8 @br_eq_i8(i8 %a, i8 %b) {
; CHECK-LABEL: br_eq_i8:
; CHECK: SUB
; CHECK: JNZ
; CHECK: MVI
; CHECK: RET
entry:
  %cmp = icmp eq i8 %a, %b
  br i1 %cmp, label %t, label %f

t:
  ret i8 1

f:
  ret i8 0
}

define i8 @br_ne_i8(i8 %a, i8 %b) {
; CHECK-LABEL: br_ne_i8:
; CHECK: SUB
; CHECK: JZ
; CHECK: MVI
; CHECK: RET
entry:
  %cmp = icmp ne i8 %a, %b
  br i1 %cmp, label %t, label %f

t:
  ret i8 1

f:
  ret i8 0
}

define i8 @br_ult_i16(i16 %a, i16 %b) {
; CHECK-LABEL: br_ult_i16:
; CHECK: SBB
; CHECK: JNC
; CHECK: MVI
; CHECK: RET
entry:
  %cmp = icmp ult i16 %a, %b
  br i1 %cmp, label %t, label %f

t:
  ret i8 1

f:
  ret i8 0
}

define i8 @br_slt_i16(i16 %a, i16 %b) {
; CHECK-LABEL: br_slt_i16:
; CHECK: XRI 128
; CHECK: SUB
; CHECK: SBB
; CHECK: MVI
; CHECK: RET
entry:
  %cmp = icmp slt i16 %a, %b
  br i1 %cmp, label %t, label %f

t:
  ret i8 1

f:
  ret i8 0
}

define i8 @br_eq_i32(i32 %a, i32 %b) {
; CHECK-LABEL: br_eq_i32:
; CHECK: CMP M
; CHECK: JNZ
; CHECK: RET
entry:
  %cmp = icmp eq i32 %a, %b
  br i1 %cmp, label %t, label %f

t:
  ret i8 1

f:
  ret i8 0
}
