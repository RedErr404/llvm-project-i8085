; RUN: llc -mattr=i8085,sram < %s -march=i8085 | FileCheck %s

; Loop idioms: counted loops, do-while patterns.
; Adapted from generic loop patterns seen in RISC-V tests.

; Simple counted loop: sum 0..n-1
define i16 @counted_loop_sum(i16 %n) {
; CHECK-LABEL: counted_loop_sum:
; CHECK: DAD SP
; CHECK: SUI
; CHECK: JNZ
; CHECK: RET
entry:
  %cmp = icmp sgt i16 %n, 0
  br i1 %cmp, label %loop, label %exit

loop:
  %i = phi i16 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i16 [ 0, %entry ], [ %sum.next, %loop ]
  %sum.next = add i16 %sum, %i
  %i.next = add i16 %i, 1
  %done = icmp eq i16 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  %result = phi i16 [ 0, %entry ], [ %sum.next, %loop ]
  ret i16 %result
}

; Do-while loop: decrement until zero
define i16 @dowhile_countdown(i16 %start) {
; CHECK-LABEL: dowhile_countdown:
; CHECK: ADD
; CHECK: ADC
; CHECK: ORA
; CHECK: JNZ
; CHECK: RET
entry:
  br label %loop

loop:
  %val = phi i16 [ %start, %entry ], [ %next, %loop ]
  %acc = phi i16 [ 0, %entry ], [ %acc.next, %loop ]
  %acc.next = add i16 %acc, %val
  %next = add i16 %val, -1
  %done = icmp eq i16 %next, 0
  br i1 %done, label %exit, label %loop

exit:
  ret i16 %acc.next
}

; Loop with i8 counter
define i8 @loop_i8_counter(i8 %n) {
; CHECK-LABEL: loop_i8_counter:
; CHECK: XRA
; CHECK: SUB
; CHECK: JNZ
; CHECK: RET
entry:
  %cmp = icmp eq i8 %n, 0
  br i1 %cmp, label %exit, label %loop

loop:
  %i = phi i8 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i8 [ 0, %entry ], [ %acc.next, %loop ]
  %acc.next = xor i8 %acc, %i
  %i.next = add i8 %i, 1
  %done = icmp eq i8 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  %result = phi i8 [ 0, %entry ], [ %acc.next, %loop ]
  ret i8 %result
}
