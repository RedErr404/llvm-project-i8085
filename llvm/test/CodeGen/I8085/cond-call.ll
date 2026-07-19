; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s

; if (cond) foo();  where foo needs no argument setup (void/no-arg) should fuse
; the skip-branch and the call into a single conditional call. The branch is the
; inverse of the call condition: `JZ skip ; CALL g` (call when non-zero) -> CNZ.

declare void @g()
declare void @h()

define void @call_if_nz(i8 %c) {
; CHECK-LABEL: call_if_nz:
; CHECK:      CNZ g
; CHECK-NOT:  CALL g
; CHECK-NOT:  JZ
entry:
  %t = icmp ne i8 %c, 0
  br i1 %t, label %do, label %join
do:
  call void @g()
  br label %join
join:
  tail call void @h()
  ret void
}

define void @call_if_z(i8 %c) {
; CHECK-LABEL: call_if_z:
; CHECK:      CZ g
; CHECK-NOT:  CALL g
entry:
  %t = icmp eq i8 %c, 0
  br i1 %t, label %do, label %join
do:
  call void @g()
  br label %join
join:
  tail call void @h()
  ret void
}

; At function end: if (c) g();  should become CNZ g ; RET (not RZ ; CALL ; RET).
define void @call_at_end(i8 %c) {
; CHECK-LABEL: call_at_end:
; CHECK:      CNZ g
; CHECK-NOT:  CALL g
; CHECK-NOT:  RZ
entry:
  %t = icmp ne i8 %c, 0
  br i1 %t, label %do, label %join
do:
  call void @g()
  br label %join
join:
  ret void
}
