; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s

; 16-bit loads/stores from/to a fixed (global) address use the 8085's direct
; LHLD / SHLD instructions (one 3-byte instruction, 16 states, no flags) instead
; of two LXI/MOV-M byte pairs.

@g = global i16 0

define i16 @load_g() {
; CHECK-LABEL: load_g:
; CHECK:       LHLD g
; CHECK-NOT:   LXI H, g
; CHECK:       RET
entry:
  %v = load i16, i16* @g
  ret i16 %v
}

define void @store_g(i16 %v) {
; CHECK-LABEL: store_g:
; CHECK:       SHLD g
; CHECK-NOT:   MOV M,
; CHECK:       RET
entry:
  store i16 %v, i16* @g
  ret void
}

; Load into HL is a single LHLD with no register moves after it.
define i16 @load_g_ret() {
; CHECK-LABEL: load_g_ret:
; CHECK:       LHLD g
; CHECK-NEXT:  MOV C, L
; CHECK-NEXT:  MOV B, H
entry:
  %v = load i16, i16* @g
  ret i16 %v
}
