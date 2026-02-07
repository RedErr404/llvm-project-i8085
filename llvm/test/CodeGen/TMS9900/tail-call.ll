; RUN: llc -march=tms9900 -O2 < %s | FileCheck %s

declare i16 @foo(i16)
declare i16 @bar(i16)

;; Simple tail call: should use B instead of BL, no prologue/epilogue
; CHECK-LABEL: tail_call_simple:
; CHECK-NOT: DECT
; CHECK-NOT: BL
; CHECK: B{{[ 	]+}}@foo
define i16 @tail_call_simple(i16 %x) {
  %r = tail call i16 @foo(i16 %x)
  ret i16 %r
}

;; Non-tail call: result is modified, must use BL
; CHECK-LABEL: not_tail_call:
; CHECK: DECT
; CHECK: BL{{[ 	]+}}@foo
define i16 @not_tail_call(i16 %x) {
  %r = call i16 @foo(i16 %x)
  %s = add i16 %r, 1
  ret i16 %s
}

;; Tail call with argument modification: still a tail call
; CHECK-LABEL: tail_call_with_arg:
; CHECK-NOT: DECT
; CHECK: INC
; CHECK: B{{[ 	]+}}@foo
define i16 @tail_call_with_arg(i16 %x) {
  %a = add i16 %x, 1
  %r = tail call i16 @foo(i16 %a)
  ret i16 %r
}

;; Conditional tail calls in both branches
; CHECK-LABEL: tail_call_diamond:
; CHECK: B{{[ 	]+}}@foo
; CHECK: B{{[ 	]+}}@bar
define i16 @tail_call_diamond(i16 %x) {
  %cmp = icmp sgt i16 %x, 10
  br i1 %cmp, label %then, label %else

then:
  %r1 = tail call i16 @foo(i16 %x)
  ret i16 %r1

else:
  %r2 = tail call i16 @bar(i16 %x)
  ret i16 %r2
}
