; RUN: llc -mattr=i8085,sram < %s -march=i8085 | FileCheck %s

; Arithmetic with overflow detection using LLVM intrinsics.
; Adapted from RISC-V arith-with-overflow.ll

declare {i16, i1} @llvm.uadd.with.overflow.i16(i16, i16)
declare {i16, i1} @llvm.usub.with.overflow.i16(i16, i16)
declare {i8, i1} @llvm.uadd.with.overflow.i8(i8, i8)

; Unsigned add with overflow, return overflow flag
define i8 @uadd_overflow_i16(i16 %a, i16 %b, i16* %out) {
; CHECK-LABEL: uadd_overflow_i16:
; CHECK: ADD L
; CHECK: ADC H
; CHECK: MOV M, C
; CHECK: INX H
; CHECK: MOV M, B
; CHECK: SUB
; CHECK: SBB
; CHECK: JNC
; CHECK: MVI A, 1
; CHECK: RET
; CHECK: MVI A, 0
; CHECK: RET
entry:
  %x = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %a, i16 %b)
  %sum = extractvalue {i16, i1} %x, 0
  %ovf = extractvalue {i16, i1} %x, 1
  store i16 %sum, i16* %out
  %r = zext i1 %ovf to i8
  ret i8 %r
}

; i8 unsigned add with overflow
define i8 @uadd_overflow_i8(i8 %a, i8 %b, i8* %out) {
; CHECK-LABEL: uadd_overflow_i8:
; CHECK: ADD C
; CHECK: STAX D
; CHECK: SUB
; CHECK: JC
; CHECK: MVI A, 0
; CHECK: RET
; CHECK: MVI A, 1
; CHECK: RET
entry:
  %x = call {i8, i1} @llvm.uadd.with.overflow.i8(i8 %a, i8 %b)
  %sum = extractvalue {i8, i1} %x, 0
  %ovf = extractvalue {i8, i1} %x, 1
  store i8 %sum, i8* %out
  %r = zext i1 %ovf to i8
  ret i8 %r
}

; Branch on overflow
define i16 @uadd_branch_overflow(i16 %a, i16 %b) {
; CHECK-LABEL: uadd_branch_overflow:
; CHECK: ADD
; CHECK: ADC
; CHECK: SBB
; CHECK: RNC
; CHECK: LXI B, -1
; CHECK: RET
entry:
  %x = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %a, i16 %b)
  %sum = extractvalue {i16, i1} %x, 0
  %ovf = extractvalue {i16, i1} %x, 1
  br i1 %ovf, label %overflow, label %nooverflow

overflow:
  ret i16 65535

nooverflow:
  ret i16 %sum
}
