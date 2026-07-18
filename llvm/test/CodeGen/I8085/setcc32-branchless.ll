; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s
; RUN: llc -mattr=i8085,sram < %s -march=i8085 -O0 -verify-machineinstrs | FileCheck %s

; A value-producing i32 comparison materializes its 0/1 result branchlessly
; instead of the SET_*_32 diamond.  The i32 operands are memory-backed GR32
; imaginary registers, so no subreg COPY is needed and the branchless form is
; used at every opt level: unsigned uses SUB_32's borrow, equality adds SUBI_32
; (SUI 1 borrows iff the difference is zero), signed biases by XOR 0x80000000
; (XRI 128 on the top byte) - all followed by SBB A;ANI 1 / INR A.  No MVI 0/1
; boolean blocks, no JMP_32_IF_* / JC / JZ diamond branches.

define i8 @ult32(i32 %a, i32 %b) {
; CHECK-LABEL: ult32:
; CHECK:       SBB A
; CHECK:       ANI 1
; CHECK-NOT:   MVI A, 1
; CHECK-NOT:   MVI M, 1
entry:
  %c = icmp ult i32 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @uge32(i32 %a, i32 %b) {
; CHECK-LABEL: uge32:
; CHECK:       SBB A
; CHECK:       INR A
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp uge i32 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @eq32(i32 %a, i32 %b) {
; CHECK-LABEL: eq32:
; CHECK:       SUI 1
; CHECK:       SBB A
; CHECK:       ANI 1
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp eq i32 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @ne32(i32 %a, i32 %b) {
; CHECK-LABEL: ne32:
; CHECK:       SUI 1
; CHECK:       SBB A
; CHECK:       INR A
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp ne i32 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @slt32(i32 %a, i32 %b) {
; CHECK-LABEL: slt32:
; CHECK:       XRI 128
; CHECK:       SBB A
; CHECK:       ANI 1
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp slt i32 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @sge32(i32 %a, i32 %b) {
; CHECK-LABEL: sge32:
; CHECK:       XRI 128
; CHECK:       SBB A
; CHECK:       INR A
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp sge i32 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @sgt32(i32 %a, i32 %b) {
; CHECK-LABEL: sgt32:
; CHECK:       XRI 128
; CHECK:       SBB A
; CHECK:       ANI 1
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp sgt i32 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @sle32(i32 %a, i32 %b) {
; CHECK-LABEL: sle32:
; CHECK:       XRI 128
; CHECK:       SBB A
; CHECK:       INR A
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp sle i32 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}
