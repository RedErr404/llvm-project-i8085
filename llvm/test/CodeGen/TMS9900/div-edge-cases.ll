; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test division edge cases:
;   - Unsigned division by power of 2 -> shift right
;   - Unsigned division by non-power-of-2 -> DIV instruction
;   - Unsigned remainder by power of 2 -> ANDI mask
;   - Signed division -> sign correction + shift
;   - Signed remainder -> ANDI with sign correction

target datalayout = "e-m:e-p:16:16-i16:16-a:0:16-n16"

; --- Unsigned div by 2 optimizes to SRL ---
; CHECK-LABEL: udiv_by_2:
; CHECK: SRL{{[ \t]+}}R0,1
; CHECK-NEXT: B{{[ \t]+}}*R11

define i16 @udiv_by_2(i16 %x) {
  %r = udiv i16 %x, 2
  ret i16 %r
}

; --- Unsigned div by 8 optimizes to SRL ---
; CHECK-LABEL: udiv_by_8:
; CHECK: SRL{{[ \t]+}}R0,3
; CHECK-NEXT: B{{[ \t]+}}*R11

define i16 @udiv_by_8(i16 %x) {
  %r = udiv i16 %x, 8
  ret i16 %r
}

; --- Unsigned div by non-power-of-2 uses DIV instruction ---
; CHECK-LABEL: udiv_by_7:
; CHECK: DIV
; CHECK: B{{[ \t]+}}*R11

define i16 @udiv_by_7(i16 %x) {
  %r = udiv i16 %x, 7
  ret i16 %r
}

; --- Unsigned remainder by 4 optimizes to ANDI mask ---
; CHECK-LABEL: urem_by_4:
; CHECK: ANDI{{[ \t]+}}R0,3
; CHECK-NEXT: B{{[ \t]+}}*R11

define i16 @urem_by_4(i16 %x) {
  %r = urem i16 %x, 4
  ret i16 %r
}

; --- Unsigned remainder by 8 optimizes to ANDI mask ---
; CHECK-LABEL: urem_by_8:
; CHECK: ANDI{{[ \t]+}}R0,7
; CHECK-NEXT: B{{[ \t]+}}*R11

define i16 @urem_by_8(i16 %x) {
  %r = urem i16 %x, 8
  ret i16 %r
}

; --- Signed div by 2: sign correction + SRA ---
; The compiler extracts the sign bit, adds it, then does arithmetic shift.
; CHECK-LABEL: sdiv_by_2:
; CHECK: SRL{{[ \t]+}}R{{[0-9]+}},15
; CHECK: SRA{{[ \t]+}}R0,1
; CHECK: B{{[ \t]+}}*R11

define i16 @sdiv_by_2(i16 %x) {
  %r = sdiv i16 %x, 2
  ret i16 %r
}

; --- Signed div by 4: sign correction + SRA ---
; CHECK-LABEL: sdiv_by_4:
; CHECK: SRA{{[ \t]+}}R{{[0-9]+}},15
; CHECK: SRL{{[ \t]+}}R{{[0-9]+}},14
; CHECK: SRA{{[ \t]+}}R0,2
; CHECK: B{{[ \t]+}}*R11

define i16 @sdiv_by_4(i16 %x) {
  %r = sdiv i16 %x, 4
  ret i16 %r
}

; --- Signed remainder by 4: uses ANDI with sign adjustment ---
; CHECK-LABEL: srem_by_4:
; CHECK: ANDI{{[ \t]+}}R{{[0-9]+}},-4
; CHECK: B{{[ \t]+}}*R11

define i16 @srem_by_4(i16 %x) {
  %r = srem i16 %x, 4
  ret i16 %r
}
