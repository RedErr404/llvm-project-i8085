; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test that the backend generates auto-increment addressing (*Rx+) for
; sequential memory access patterns in loops. The peephole pass combines
; a load/store through a pointer with a subsequent pointer increment into
; a single auto-increment instruction.

; --- Word copy loop should use auto-increment ---
; CHECK-LABEL: word_copy_loop:
; CHECK: MOV{{[ \t]+}}*R{{[0-9]+}}+,R{{[0-9]+}}
; CHECK: MOV{{[ \t]+}}R{{[0-9]+}},*R{{[0-9]+}}+

define void @word_copy_loop(ptr %dst, ptr %src, i16 %n) {
entry:
  br label %loop
loop:
  %i = phi i16 [0, %entry], [%next, %loop]
  %sp = getelementptr i16, ptr %src, i16 %i
  %dp = getelementptr i16, ptr %dst, i16 %i
  %v = load i16, ptr %sp
  store i16 %v, ptr %dp
  %next = add i16 %i, 1
  %cmp = icmp ult i16 %next, %n
  br i1 %cmp, label %loop, label %done
done:
  ret void
}

; --- Sum array using auto-increment load ---
; CHECK-LABEL: sum_array:
; CHECK: *R{{[0-9]+}}+

define i16 @sum_array(ptr %arr, i16 %n) {
entry:
  br label %loop
loop:
  %i = phi i16 [0, %entry], [%next, %loop]
  %sum = phi i16 [0, %entry], [%newsum, %loop]
  %p = getelementptr i16, ptr %arr, i16 %i
  %v = load i16, ptr %p
  %newsum = add i16 %sum, %v
  %next = add i16 %i, 1
  %cmp = icmp ult i16 %next, %n
  br i1 %cmp, label %loop, label %done
done:
  ret i16 %newsum
}
