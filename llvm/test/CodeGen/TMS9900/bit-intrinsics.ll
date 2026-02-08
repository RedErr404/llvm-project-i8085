; RUN: llc -march=tms9900 -O2 < %s | FileCheck %s
;
; Test bit manipulation intrinsics: ctlz, cttz, ctpop.
; All three are Expand on TMS9900, so LLVM generates inline branchless
; bit manipulation sequences (no libcalls).

declare i16 @llvm.ctlz.i16(i16, i1)
declare i16 @llvm.cttz.i16(i16, i1)
declare i16 @llvm.ctpop.i16(i16)

; --- Count leading zeros ---
; Expands to branchless bit manipulation: propagate MSB down, then popcount.
; Returns 16 for input of 0.
; CHECK-LABEL: ctlz_val:
; CHECK-NOT: BL
; CHECK: SRL
; CHECK: SOC
; CHECK: INV
; CHECK: ANDI{{[ \t]+}}R{{[0-9]+}},31
; CHECK: B{{[ \t]+}}*R11

define i16 @ctlz_val(i16 %x) {
  %r = call i16 @llvm.ctlz.i16(i16 %x, i1 false)
  ret i16 %r
}

; --- Count trailing zeros ---
; Expands to: isolate lowest set bit (x & -x), then popcount-based counting.
; Returns 16 for input of 0.
; CHECK-LABEL: cttz_val:
; CHECK-NOT: BL
; CHECK: DEC{{[ \t]+}}R{{[0-9]+}}
; CHECK: SZC
; CHECK: ANDI{{[ \t]+}}R{{[0-9]+}},31
; CHECK: B{{[ \t]+}}*R11

define i16 @cttz_val(i16 %x) {
  %r = call i16 @llvm.cttz.i16(i16 %x, i1 false)
  ret i16 %r
}

; --- Population count ---
; Expands to branchless sideways addition.
; No branches, no libcalls - pure arithmetic.
; CHECK-LABEL: ctpop_val:
; CHECK-NOT: BL
; CHECK: SRL
; CHECK: ANDI{{[ \t]+}}R{{[0-9]+}},21845
; CHECK: ANDI{{[ \t]+}}R{{[0-9]+}},13107
; CHECK: ANDI{{[ \t]+}}R{{[0-9]+}},3855
; CHECK: ANDI{{[ \t]+}}R{{[0-9]+}},31
; CHECK: B{{[ \t]+}}*R11

define i16 @ctpop_val(i16 %x) {
  %r = call i16 @llvm.ctpop.i16(i16 %x)
  ret i16 %r
}
