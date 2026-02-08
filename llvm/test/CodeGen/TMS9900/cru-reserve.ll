; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s --check-prefix=FREE
; RUN: llc -mtriple=tms9900 -mattr=+reserve-cru -O2 < %s | FileCheck %s --check-prefix=RESERVE
;
; Test that R12 is available as a general-purpose register when
; FeatureReserveCRU is off (default), and NOT available when it is on.
;
; This function has enough register pressure (12 volatile loads held
; simultaneously) to force the allocator to use R12 when available.

@g0 = external global i16
@g1 = external global i16
@g2 = external global i16
@g3 = external global i16
@g4 = external global i16
@g5 = external global i16
@g6 = external global i16
@g7 = external global i16
@g8 = external global i16
@g9 = external global i16
@g10 = external global i16
@g11 = external global i16

; FREE-LABEL: fill_regs:
; FREE: R12
;
; RESERVE-LABEL: fill_regs:
; RESERVE-NOT: R12

define void @fill_regs() {
  %v0 = load volatile i16, ptr @g0
  %v1 = load volatile i16, ptr @g1
  %v2 = load volatile i16, ptr @g2
  %v3 = load volatile i16, ptr @g3
  %v4 = load volatile i16, ptr @g4
  %v5 = load volatile i16, ptr @g5
  %v6 = load volatile i16, ptr @g6
  %v7 = load volatile i16, ptr @g7
  %v8 = load volatile i16, ptr @g8
  %v9 = load volatile i16, ptr @g9
  %v10 = load volatile i16, ptr @g10
  %v11 = load volatile i16, ptr @g11
  store volatile i16 %v0, ptr @g0
  store volatile i16 %v1, ptr @g1
  store volatile i16 %v2, ptr @g2
  store volatile i16 %v3, ptr @g3
  store volatile i16 %v4, ptr @g4
  store volatile i16 %v5, ptr @g5
  store volatile i16 %v6, ptr @g6
  store volatile i16 %v7, ptr @g7
  store volatile i16 %v8, ptr @g8
  store volatile i16 %v9, ptr @g9
  store volatile i16 %v10, ptr @g10
  store volatile i16 %v11, ptr @g11
  ret void
}
