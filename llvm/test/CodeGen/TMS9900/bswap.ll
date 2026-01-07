; RUN: llc -march=tms9900 -O0 < %s | FileCheck %s

declare i16 @llvm.bswap.i16(i16)

; CHECK-LABEL: bswap
; CHECK: SWPB{{[ \t]+}}R0

define i16 @bswap(i16 %x) {
entry:
  %v = call i16 @llvm.bswap.i16(i16 %x)
  ret i16 %v
}
