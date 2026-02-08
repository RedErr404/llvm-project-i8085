; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test that XOR Rx,Rx produces CLR Rx.
;
; LLVM's IR optimizer folds xor x,x to constant 0 before codegen, so the
; path exercised here is: constant 0 -> LI Rx,0 -> peephole CLR Rx.
; The peephole also handles the case where the backend generates an actual
; XOR Rx,Rx instruction (XORrr -> CLRr).

; CHECK-LABEL: xor_self:
; CHECK: CLR{{[ \t]+}}R0
; CHECK-NOT: XOR
; CHECK: B{{[ \t]+}}*R11

define i16 @xor_self(i16 %x) {
  %r = xor i16 %x, %x
  ret i16 %r
}
