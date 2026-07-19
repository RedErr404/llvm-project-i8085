; RUN: llc -mattr=i8085,sram < %s -march=i8085 | FileCheck %s

; Signbit testing patterns.
; Adapted from RISC-V signbit-test.ll

; Test if i8 is negative (shift right 7 to get sign bit)
define i8 @is_negative_i8(i8 %x) {
; CHECK-LABEL: is_negative_i8:
; CHECK: RLC
; CHECK: ANI 1
; CHECK: RET
entry:
  %cmp = icmp slt i8 %x, 0
  %r = zext i1 %cmp to i8
  ret i8 %r
}

; Test high bit of i8 via mask and compare
define i8 @test_high_bit_i8(i8 %x) {
; CHECK-LABEL: test_high_bit_i8:
; CHECK: RLC
; CHECK: ANI 1
; CHECK: RET
entry:
  %masked = and i8 %x, 128
  %cmp = icmp ne i8 %masked, 0
  %r = zext i1 %cmp to i8
  ret i8 %r
}

; Branch on sign bit of i16
define i16 @branch_on_sign(i16 %x) {
; CHECK-LABEL: branch_on_sign:
; CHECK: ORA A
; CHECK: RET
entry:
  %cmp = icmp slt i16 %x, 0
  br i1 %cmp, label %neg, label %pos

neg:
  %r1 = sub i16 0, %x
  ret i16 %r1

pos:
  ret i16 %x
}
