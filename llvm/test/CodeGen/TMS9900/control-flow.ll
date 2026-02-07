; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test control flow patterns: if/else (diamond), loops, nested conditions.

; --- Diamond (if/else) with phi ---
; CHECK-LABEL: diamond:
; CHECK: CI{{[ \t]+}}R0
; CHECK: B{{[ \t]+}}*R11

define i16 @diamond(i16 %a, i16 %b) {
entry:
  %cmp = icmp sgt i16 %a, 0
  br i1 %cmp, label %then, label %else
then:
  %t = add i16 %a, %b
  br label %merge
else:
  %e = sub i16 %a, %b
  br label %merge
merge:
  %r = phi i16 [%t, %then], [%e, %else]
  ret i16 %r
}

; --- Simple counted loop ---
; Uses C for comparison and a conditional jump to loop back.
; CHECK-LABEL: counted_loop:
; CHECK: C{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: JNE
; CHECK: B{{[ \t]+}}*R11

define i16 @counted_loop(i16 %n) {
entry:
  br label %loop
loop:
  %i = phi i16 [0, %entry], [%next, %loop]
  %sum = phi i16 [0, %entry], [%newsum, %loop]
  %newsum = add i16 %sum, %i
  %next = add i16 %i, 1
  %cmp = icmp eq i16 %next, %n
  br i1 %cmp, label %done, label %loop
done:
  ret i16 %newsum
}

; --- Nested if ---
; CHECK-LABEL: nested_if:
; CHECK: CI
; CHECK: B{{[ \t]+}}*R11

define i16 @nested_if(i16 %a, i16 %b) {
entry:
  %c1 = icmp sgt i16 %a, 0
  br i1 %c1, label %outer_then, label %done
outer_then:
  %c2 = icmp sgt i16 %b, 0
  br i1 %c2, label %inner_then, label %done
inner_then:
  %r = add i16 %a, %b
  br label %done
done:
  %result = phi i16 [0, %entry], [1, %outer_then], [%r, %inner_then]
  ret i16 %result
}
