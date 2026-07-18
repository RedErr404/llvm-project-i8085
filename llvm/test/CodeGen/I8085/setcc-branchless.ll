; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s

; A value-producing i8 unsigned/equality comparison materializes its 0/1 result
; branchlessly (SUB ; [SUI 1 ;] SBB A ; ANI 1 / INR A) instead of a
; SET_*_8 boolean diamond that branches to MVI 0 / MVI 1 blocks.

define i8 @eq(i8 %a, i8 %b) {
; CHECK-LABEL: eq:
; CHECK:       SUB
; CHECK:       SBB A
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp eq i8 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @ne(i8 %a, i8 %b) {
; CHECK-LABEL: ne:
; CHECK:       SBB A
; CHECK:       INR A
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp ne i8 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @ult(i8 %a, i8 %b) {
; CHECK-LABEL: ult:
; CHECK:       SUB
; CHECK:       SBB A
; CHECK:       ANI 1
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp ult i8 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @uge(i8 %a, i8 %b) {
; CHECK-LABEL: uge:
; CHECK:       SBB A
; CHECK:       INR A
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp uge i8 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @ugt(i8 %a, i8 %b) {
; CHECK-LABEL: ugt:
; CHECK:       SBB A
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp ugt i8 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @ule(i8 %a, i8 %b) {
; CHECK-LABEL: ule:
; CHECK:       SBB A
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp ule i8 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @slt(i8 %a, i8 %b) {
; CHECK-LABEL: slt:
; CHECK:       XRI 128
; CHECK:       SBB A
; CHECK:       ANI 1
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp slt i8 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @sge(i8 %a, i8 %b) {
; CHECK-LABEL: sge:
; CHECK:       XRI 128
; CHECK:       SBB A
; CHECK:       INR A
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp sge i8 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @sgt(i8 %a, i8 %b) {
; CHECK-LABEL: sgt:
; CHECK:       SBB A
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp sgt i8 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @sle(i8 %a, i8 %b) {
; CHECK-LABEL: sle:
; CHECK:       SBB A
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp sle i8 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}
