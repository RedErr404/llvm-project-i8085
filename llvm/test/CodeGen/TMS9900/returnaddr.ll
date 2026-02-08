; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test the llvm.returnaddress intrinsic.
; On TMS9900, the return address lives in R11 (the link register).
; For depth 0, this should simply copy R11 to the return register.
;
; The TMS9900 backend now correctly lowers RETURNADDR to a copy from R11.

declare ptr @llvm.returnaddress(i32)

; CHECK-LABEL: get_return_addr:
; CHECK:       MOV R11,R0
; CHECK:       B *R11

define ptr @get_return_addr() {
  %ra = call ptr @llvm.returnaddress(i32 0)
  ret ptr %ra
}
