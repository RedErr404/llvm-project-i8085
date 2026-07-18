; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s

; Simple loop with phi nodes to exercise backedges.

define i16 @loop_sum(i16 %n) {
; CHECK-LABEL: loop_sum:
; CHECK: JC
; CHECK: RET
entry:
  br label %loop

loop:
  %i = phi i16 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i16 [ 0, %entry ], [ %acc.next, %loop ]
  %acc.next = add i16 %acc, %i
  %i.next = add i16 %i, 1
  %cmp = icmp ult i16 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret i16 %acc.next
}
