; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test AND pseudo expansion optimizations:
; - Dead source elimination: skip restore INV when source is killed
; - Double INV cancellation: consecutive INV Rx; INV Rx pairs are deleted

; --- AND with dead source: restore INV should be eliminated ---
; The source register %b is not used after the AND, so the restore INV
; is unnecessary. Sequence should be: INV + SZC (2 instructions).
;
; CHECK-LABEL: and_dead_src:
; CHECK:       INV
; CHECK-NEXT:  SZC
; CHECK-NEXT:  B{{[ \t]+}}*R11

define i16 @and_dead_src(i16 %a, i16 %b) {
  %r = and i16 %a, %b
  ret i16 %r
}

; --- AND with live source: restore INV must be kept ---
; The source register %b is used after the AND, so the restore INV is
; needed. Sequence should be: INV + SZC + INV (3 instructions).
;
; CHECK-LABEL: and_live_src:
; CHECK:       INV
; CHECK-NEXT:  SZC
; CHECK-NEXT:  INV
; CHECK:       B{{[ \t]+}}*R11

define i16 @and_live_src(i16 %a, i16 %b) {
  %r = and i16 %a, %b
  %s = add i16 %r, %b
  ret i16 %s
}

; --- Two consecutive ANDs with same source: double INV cancellation ---
; The first AND's restore INV and the second AND's initial INV cancel.
; The second AND's source is killed, so its restore INV is also eliminated.
; Result: INV + SZC + SZC (3 instructions instead of 7).
;
; CHECK-LABEL: and_multi:
; CHECK:       INV
; CHECK-NEXT:  SZC
; CHECK-NEXT:  SZC
; CHECK-NOT:   INV
; CHECK:       B{{[ \t]+}}*R11

define i16 @and_multi(i16 %a, i16 %b, i16 %c) {
  %r1 = and i16 %a, %b
  %r2 = and i16 %c, %b
  %r = add i16 %r1, %r2
  ret i16 %r
}
