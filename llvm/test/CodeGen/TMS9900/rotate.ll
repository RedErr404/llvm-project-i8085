; RUN: llc -march=tms9900 -O2 < %s | FileCheck %s
;
; Test rotation operations.
; TMS9900 expands rotates to shift+shift+or sequences.
; LLVM recognizes (x << n) | (x >> (16-n)) patterns as rotates,
; which then get expanded.

declare i16 @llvm.fshl.i16(i16, i16, i16)

; --- Rotate left by 4 via fshl intrinsic ---
; CHECK-LABEL: rotl_4:
; CHECK: SLA{{[ \t]}}
; CHECK: SRL{{[ \t]}}
; CHECK: SOC{{[ \t]}}
; CHECK: B{{[ \t]+}}*R11

define i16 @rotl_4(i16 %x) {
  %r = call i16 @llvm.fshl.i16(i16 %x, i16 %x, i16 4)
  ret i16 %r
}

; --- Rotate right by 4 via shift+or pattern ---
; CHECK-LABEL: rotr_4:
; CHECK: SLA{{[ \t]}}
; CHECK: SRL{{[ \t]}}
; CHECK: SOC{{[ \t]}}
; CHECK: B{{[ \t]+}}*R11

define i16 @rotr_4(i16 %x) {
  %r = lshr i16 %x, 4
  %l = shl i16 %x, 12
  %rot = or i16 %l, %r
  ret i16 %rot
}

; --- Rotate left by 5 via shift+or pattern ---
; CHECK-LABEL: rotl_pattern:
; CHECK: SLA{{[ \t]}}
; CHECK: SRL{{[ \t]}}
; CHECK: SOC{{[ \t]}}
; CHECK: B{{[ \t]+}}*R11

define i16 @rotl_pattern(i16 %x) {
  %l = shl i16 %x, 5
  %r = lshr i16 %x, 11
  %rot = or i16 %l, %r
  ret i16 %rot
}
