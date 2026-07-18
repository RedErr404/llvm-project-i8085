; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s
; RUN: llc -mattr=i8085,sram < %s -march=i8085 -O0 -verify-machineinstrs | FileCheck %s --check-prefix=O0

; A value-producing i16 comparison materializes its 0/1 result branchlessly
; instead of the SET_*_16 boolean diamond.  Unsigned magnitude uses SUB_16's
; borrow (works at every opt level); equality (XRA combine + SUI 1) and signed
; (high-byte 0x80 bias) extract operand bytes via subreg COPY and so only fire
; at O1+ (the -O0 fast allocator mishandles that, falling back to the diamond).

define i8 @ult16(i16 %a, i16 %b) {
; CHECK-LABEL: ult16:
; CHECK:       SUB
; CHECK:       SBB
; CHECK:       SBB A
; CHECK:       ANI 1
; CHECK-NOT:   MVI A, 1
; O0-LABEL: ult16:
; O0:       SBB A
; O0:       ANI 1
entry:
  %c = icmp ult i16 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @uge16(i16 %a, i16 %b) {
; CHECK-LABEL: uge16:
; CHECK:       SUB
; CHECK:       SBB A
; CHECK:       INR A
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp uge i16 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @ugt16(i16 %a, i16 %b) {
; CHECK-LABEL: ugt16:
; CHECK:       SUB
; CHECK:       SBB A
; CHECK:       ANI 1
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp ugt i16 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @ule16(i16 %a, i16 %b) {
; CHECK-LABEL: ule16:
; CHECK:       SUB
; CHECK:       SBB A
; CHECK:       INR A
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp ule i16 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @eq16(i16 %a, i16 %b) {
; CHECK-LABEL: eq16:
; CHECK:       XRA
; CHECK:       ORA
; CHECK:       SUI 1
; CHECK:       SBB A
; CHECK:       ANI 1
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp eq i16 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @ne16(i16 %a, i16 %b) {
; CHECK-LABEL: ne16:
; CHECK:       XRA
; CHECK:       SUI 1
; CHECK:       SBB A
; CHECK:       INR A
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp ne i16 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @slt16(i16 %a, i16 %b) {
; CHECK-LABEL: slt16:
; CHECK:       XRI 128
; CHECK:       SUB
; CHECK:       SBB A
; CHECK:       ANI 1
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp slt i16 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @sge16(i16 %a, i16 %b) {
; CHECK-LABEL: sge16:
; CHECK:       XRI 128
; CHECK:       SBB A
; CHECK:       INR A
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp sge i16 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @sgt16(i16 %a, i16 %b) {
; CHECK-LABEL: sgt16:
; CHECK:       XRI 128
; CHECK:       SBB A
; CHECK:       ANI 1
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp sgt i16 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @sle16(i16 %a, i16 %b) {
; CHECK-LABEL: sle16:
; CHECK:       XRI 128
; CHECK:       SBB A
; CHECK:       INR A
; CHECK-NOT:   MVI A, 1
entry:
  %c = icmp sle i16 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}
