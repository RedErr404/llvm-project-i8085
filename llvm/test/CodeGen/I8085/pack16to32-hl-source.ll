; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s

; Building an i32 from an i16 that the register allocator placed in HL packs it
; into the GR32 scratch. Computing the scratch address clobbers HL, so the low
; half is first saved into another pair. A release-build assert()-with-side-
; effects bug (getPairRegs called only inside assert) left those temp registers
; unallocated ($noreg), producing `MOV $noreg,$l` / `MOV_M $noreg` - garbage at
; runtime, a verifier error, and an asm-printer crash. The stores must use real
; registers, so -verify-machineinstrs must pass.

@r = global i16 0

define void @pack_hl_source() {
; CHECK-LABEL: pack_hl_source:
; CHECK-NOT: $noreg
  br label %loop
loop:
  %acc = phi i16 [ 0, %0 ], [ %acc2, %loop ]
  %v = phi i16 [ 1, %0 ], [ %vn, %loop ]
  %v32 = zext i16 %v to i32
  %hi = shl i32 %v32, 16
  %notv = xor i16 %v, -1
  %lo = zext i16 %notv to i32
  %w = or i32 %hi, %lo
  %sh = lshr i32 %w, 1
  %sht = trunc i32 %sh to i16
  %acc2 = add i16 %acc, %sht
  %vn = add i16 %v, 6553
  %done = icmp eq i16 %vn, 0
  br i1 %done, label %exit, label %loop
exit:
  store volatile i16 %acc2, ptr @r
  ret void
}
