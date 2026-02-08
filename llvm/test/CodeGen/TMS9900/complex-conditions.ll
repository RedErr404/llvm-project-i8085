; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test complex boolean conditions: short-circuit AND, short-circuit OR,
; and nested conditions. These exercise multiple compare+branch sequences.

; --- Short-circuit AND: if (a > 0 && b < 10) ---
; Both conditions must be true. The backend should emit two comparisons
; with early-exit to the else block if either fails.
;
; CHECK-LABEL: test_and:
; CHECK:       CI R0,1
; CHECK-NEXT:  JLT [[ELSE:LBB[0-9_]+]]
; CHECK:       CI R1,9
; CHECK-NEXT:  JGT [[ELSE]]
; CHECK:       LI R0,1
; CHECK:       B *R11
; CHECK:       [[ELSE]]:
; CHECK:       CLR R0
; CHECK:       B *R11

define i16 @test_and(i16 %a, i16 %b) {
entry:
  %c1 = icmp sgt i16 %a, 0
  %c2 = icmp slt i16 %b, 10
  %and = and i1 %c1, %c2
  br i1 %and, label %then, label %else
then:
  ret i16 1
else:
  ret i16 0
}

; --- Short-circuit OR: if (a == 0 || b == 0) ---
; Either condition being true suffices. The backend should emit two
; zero-tests with early-exit to the then block on first success.
;
; CHECK-LABEL: test_or:
; CHECK:       MOV R0,R0
; CHECK-NEXT:  JEQ [[THEN:LBB[0-9_]+]]
; CHECK:       MOV R1,R1
; CHECK-NEXT:  JEQ [[THEN]]
; CHECK:       CLR R0
; CHECK:       B *R11
; CHECK:       [[THEN]]:
; CHECK:       LI R0,1
; CHECK:       B *R11

define i16 @test_or(i16 %a, i16 %b) {
entry:
  %c1 = icmp eq i16 %a, 0
  %c2 = icmp eq i16 %b, 0
  %or = or i1 %c1, %c2
  br i1 %or, label %then, label %else
then:
  ret i16 1
else:
  ret i16 0
}

; --- Nested condition with explicit CFG: if (a > 0 && b < 10) ---
; Uses explicit basic blocks to force two sequential compare+branch pairs.
;
; CHECK-LABEL: test_nested:
; CHECK:       CI R0,1
; CHECK:       JLT [[ELSE2:LBB[0-9_]+]]
; CHECK:       CI R1,9
; CHECK:       JGT [[ELSE2]]
; CHECK:       A R1,R0
; CHECK:       B *R11
; CHECK:       [[ELSE2]]:
; CHECK:       CLR R0
; CHECK:       B *R11

define i16 @test_nested(i16 %a, i16 %b) {
entry:
  %c1 = icmp sgt i16 %a, 0
  br i1 %c1, label %check_b, label %else

check_b:
  %c2 = icmp slt i16 %b, 10
  br i1 %c2, label %then, label %else

then:
  %r = add i16 %a, %b
  ret i16 %r

else:
  ret i16 0
}
