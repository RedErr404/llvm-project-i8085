; RUN: llc -march=tms9900 -O2 < %s | FileCheck %s

; Test that variable shifts guard against count=0.
; TMS9900 hardware quirk: SLA/SRA/SRL with count-from-R0 treats R0=0 as
; shift-by-16 (not shift-by-0). The backend must emit a JEQ guard to skip
; the shift instruction when the count is zero.

; --- Variable left shift (shl) ---
; CMPBRri expands to CI+JEQ (or peephole MOV Rx,Rx + JEQ) atomically.
; The MOV to R0 is in the shift block, after the guard.
define i16 @var_shl(i16 %x, i16 %n) {
; CHECK-LABEL: var_shl:
; CHECK:       JEQ
; CHECK:       MOV {{R[0-9]+}},R0
; CHECK:       SLA {{R[0-9]+}},0
  %r = shl i16 %x, %n
  ret i16 %r
}

; --- Variable logical right shift (lshr) ---
define i16 @var_lshr(i16 %x, i16 %n) {
; CHECK-LABEL: var_lshr:
; CHECK:       JEQ
; CHECK:       MOV {{R[0-9]+}},R0
; CHECK:       SRL {{R[0-9]+}},0
  %r = lshr i16 %x, %n
  ret i16 %r
}

; --- Variable arithmetic right shift (ashr) ---
define i16 @var_ashr(i16 %x, i16 %n) {
; CHECK-LABEL: var_ashr:
; CHECK:       JEQ
; CHECK:       MOV {{R[0-9]+}},R0
; CHECK:       SRA {{R[0-9]+}},0
  %r = ashr i16 %x, %n
  ret i16 %r
}

; --- Constant shift should NOT have a JEQ guard ---
define i16 @const_shl(i16 %x) {
; CHECK-LABEL: const_shl:
; CHECK:       SLA {{R[0-9]+}},3
; CHECK-NOT:   JEQ
  %r = shl i16 %x, 3
  ret i16 %r
}
