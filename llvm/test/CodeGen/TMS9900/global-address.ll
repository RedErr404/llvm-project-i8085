; RUN: llc -march=tms9900 -O0 < %s | FileCheck %s
;
; Test global variable access patterns.
; The TMS9900 uses symbolic/absolute addressing mode for globals:
;   MOV @symbol,Rx   (load)
;   MOV Rx,@symbol   (store)
;   LI Rx,symbol     (address-of)

@g16 = external global i16
@g8 = external global i8
@garr = external global [10 x i16]

; --- Load from global i16 ---
; CHECK-LABEL: load_global_i16:
; CHECK: MOV{{[ \t]+}}@g16,R0
; CHECK: B{{[ \t]+}}*R11

define i16 @load_global_i16() {
  %v = load i16, ptr @g16
  ret i16 %v
}

; --- Store to global i16 ---
; CHECK-LABEL: store_global_i16:
; CHECK: MOV{{[ \t]+}}R0,@g16
; CHECK: B{{[ \t]+}}*R11

define void @store_global_i16(i16 %val) {
  store i16 %val, ptr @g16
  ret void
}

; --- Address-of global ---
; CHECK-LABEL: addr_of_global:
; CHECK: LI{{[ \t]+}}R0,g16
; CHECK: B{{[ \t]+}}*R11

define ptr @addr_of_global() {
  ret ptr @g16
}

; --- Load global i8 (byte) ---
; Byte load uses MOVB and shift to extract byte value.
; CHECK-LABEL: load_global_i8:
; CHECK: MOVB{{[ \t]+}}@g8,R0
; CHECK: SRL{{[ \t]+}}R0,8
; CHECK: B{{[ \t]+}}*R11

define i8 @load_global_i8() {
  %v = load i8, ptr @g8
  ret i8 %v
}

; --- Store global i8 (byte) ---
; Byte store uses SWPB (peephole-optimized from SLA 8) and MOVB.
; CHECK-LABEL: store_global_i8:
; CHECK: SWPB{{[ \t]+}}R0
; CHECK: MOVB{{[ \t]+}}R0,@g8
; CHECK: B{{[ \t]+}}*R11

define void @store_global_i8(i8 %val) {
  store i8 %val, ptr @g8
  ret void
}

; --- Address-of global array ---
; CHECK-LABEL: addr_of_array:
; CHECK: LI{{[ \t]+}}R0,garr
; CHECK: B{{[ \t]+}}*R11

define ptr @addr_of_array() {
  ret ptr @garr
}
