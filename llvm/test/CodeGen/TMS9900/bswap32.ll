; RUN: llc -march=tms9900 -O2 < %s | FileCheck %s
;
; Test byte swap operations.
; i16 bswap is Legal (uses SWPB instruction).
; i32 bswap is Expand: generates SWPB on each half-word plus a word swap.

declare i32 @llvm.bswap.i32(i32)
declare i16 @llvm.bswap.i16(i16)

; --- 32-bit byte swap ---
; Reverses all 4 bytes: ABCD -> DCBA
; Should SWPB both halves and swap the word order.
; CHECK-LABEL: swap32:
; CHECK: SWPB
; CHECK: SWPB
; CHECK: B{{[ \t]+}}*R11

define i32 @swap32(i32 %x) {
  %r = call i32 @llvm.bswap.i32(i32 %x)
  ret i32 %r
}

; --- 16-bit byte swap (sanity check) ---
; Should use the native SWPB instruction.
; CHECK-LABEL: swap16:
; CHECK: SWPB{{[ \t]+}}R0
; CHECK: B{{[ \t]+}}*R11

define i16 @swap16(i16 %x) {
  %r = call i16 @llvm.bswap.i16(i16 %x)
  ret i16 %r
}
