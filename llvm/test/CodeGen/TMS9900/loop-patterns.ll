; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test various loop patterns: do-while, step-by-2, loop with break,
; and counted down-to-zero.

; --- Do-while (bottom-tested) loop ---
; The loop body executes at least once, with the condition test at the bottom.
; Expect: loop body with A (accumulate), INC (increment), C + conditional
; jump back to loop top.
;
; CHECK-LABEL: do_while:
; CHECK:       CLR {{R[0-9]+}}
; CHECK:       CLR {{R[0-9]+}}
; CHECK:       [[LOOP:LBB[0-9_]+]]:
; CHECK:       A {{R[0-9]+}},{{R[0-9]+}}
; CHECK:       INC {{R[0-9]+}}
; CHECK:       C {{R[0-9]+}},{{R[0-9]+}}
; CHECK:       JL [[LOOP]]
; CHECK:       B *R11

define i16 @do_while(i16 %n) {
entry:
  br label %body
body:
  %i = phi i16 [0, %entry], [%next, %body]
  %sum = phi i16 [0, %entry], [%newsum, %body]
  %newsum = add i16 %sum, %i
  %next = add i16 %i, 1
  %cmp = icmp ult i16 %next, %n
  br i1 %cmp, label %body, label %done
done:
  ret i16 %newsum
}

; --- Loop decrementing by 2 ---
; Iterates backwards stepping by 2. Should generate DECT for the
; decrement-by-2 operation on TMS9900.
;
; CHECK-LABEL: loop_step2:
; CHECK:       [[LOOP2:LBB[0-9_]+]]:
; CHECK:       DECT {{R[0-9]+}}
; CHECK:       JGT [[LOOP2]]
; CHECK:       B *R11

define i16 @loop_step2(ptr %arr, i16 %n) {
entry:
  br label %loop
loop:
  %i = phi i16 [%n, %entry], [%next, %loop]
  %sum = phi i16 [0, %entry], [%newsum, %loop]
  %ptr = getelementptr i16, ptr %arr, i16 %i
  %val = load i16, ptr %ptr
  %newsum = add i16 %sum, %val
  %next = sub i16 %i, 2
  %cmp = icmp sgt i16 %next, 0
  br i1 %cmp, label %loop, label %done
done:
  ret i16 %newsum
}

; --- Loop with early break ---
; Searches an array for a zero element, breaking out early when found.
; Tests the pattern of a conditional branch exiting the loop mid-body.
;
; CHECK-LABEL: loop_break:
; CHECK:       [[LOOP3:LBB[0-9_]+]]:
; CHECK:       MOV *R0+,{{R[0-9]+}}
; CHECK:       JEQ
; CHECK:       B *R11

define i16 @loop_break(ptr %arr, i16 %n) {
entry:
  br label %loop
loop:
  %i = phi i16 [0, %entry], [%next, %cont]
  %ptr = getelementptr i16, ptr %arr, i16 %i
  %val = load i16, ptr %ptr
  %is_zero = icmp eq i16 %val, 0
  br i1 %is_zero, label %done, label %cont
cont:
  %next = add i16 %i, 1
  %cmp = icmp ult i16 %next, %n
  br i1 %cmp, label %loop, label %done
done:
  %result = phi i16 [%val, %loop], [%i, %cont]
  ret i16 %result
}

; --- Counted down-to-zero loop ---
; Common TMS9900 idiom: decrement a counter and branch while non-zero.
; Should generate DEC + JNE (test-and-branch on zero flag).
;
; CHECK-LABEL: count_down:
; CHECK:       [[LOOP4:LBB[0-9_]+]]:
; CHECK:       A {{R[0-9]+}},R0
; CHECK:       DEC {{R[0-9]+}}
; CHECK-NEXT:  JNE [[LOOP4]]
; CHECK:       B *R11

define i16 @count_down(i16 %n) {
entry:
  br label %loop
loop:
  %i = phi i16 [%n, %entry], [%next, %loop]
  %sum = phi i16 [0, %entry], [%newsum, %loop]
  %newsum = add i16 %sum, %i
  %next = sub i16 %i, 1
  %cmp = icmp ne i16 %next, 0
  br i1 %cmp, label %loop, label %done
done:
  ret i16 %newsum
}
