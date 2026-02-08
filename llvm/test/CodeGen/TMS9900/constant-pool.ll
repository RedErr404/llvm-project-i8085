; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test constant materialization patterns:
;   - Arbitrary 16-bit constants: LI Rx, imm
;   - Zero (0): CLR Rx (peephole from LI Rx, 0)
;   - All ones (-1/0xFFFF): SETO Rx (peephole from LI Rx, -1)
;   - 32-bit constants: two LI instructions (high:R0, low:R1)
;   - 32-bit zero: two CLR instructions

target datalayout = "e-m:e-p:16:16-i16:16-a:0:16-n16"

; --- Large 16-bit constant: LI ---
; CHECK-LABEL: large_const:
; CHECK: LI{{[ \t]+}}R0,32767
; CHECK-NEXT: B{{[ \t]+}}*R11

define i16 @large_const() {
  ret i16 32767
}

; --- -1 constant: SETO (peephole optimization) ---
; CHECK-LABEL: neg_const:
; CHECK: SETO{{[ \t]+}}R0
; CHECK-NEXT: B{{[ \t]+}}*R11

define i16 @neg_const() {
  ret i16 -1
}

; --- Zero constant: CLR (peephole optimization) ---
; CHECK-LABEL: zero_const:
; CHECK: CLR{{[ \t]+}}R0
; CHECK-NEXT: B{{[ \t]+}}*R11

define i16 @zero_const() {
  ret i16 0
}

; --- 32-bit constant 0x12345678: two LI instructions ---
; High word 0x1234 = 4660, low word 0x5678 = 22136
; CHECK-LABEL: const32:
; CHECK: LI{{[ \t]+}}R0,4660
; CHECK-NEXT: LI{{[ \t]+}}R1,22136
; CHECK-NEXT: B{{[ \t]+}}*R11

define i32 @const32() {
  ret i32 305419896
}

; --- Negative 16-bit constant: LI with negative value ---
; CHECK-LABEL: hex_const:
; CHECK: LI{{[ \t]+}}R0,-256
; CHECK-NEXT: B{{[ \t]+}}*R11

define i16 @hex_const() {
  ret i16 -256
}

; --- 32-bit zero: two CLR instructions ---
; CHECK-LABEL: const32_zero:
; CHECK: CLR{{[ \t]+}}R0
; CHECK-NEXT: CLR{{[ \t]+}}R1
; CHECK-NEXT: B{{[ \t]+}}*R11

define i32 @const32_zero() {
  ret i32 0
}
