; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test the llvm.returnaddress intrinsic.
; On TMS9900, the return address lives in R11 (the link register).
; For depth 0, this should simply copy R11 to the return register.
;
; XFAIL: *
; NOTE: This test is expected to fail because the TMS9900 backend does not
; yet fully implement RETURNADDR lowering (crashes in MCInstLower).
; When the bug is fixed, remove the XFAIL line.

declare ptr @llvm.returnaddress(i32)

; CHECK-LABEL: get_return_addr:
; CHECK:       MOV R11,R0
; CHECK:       B *R11

define ptr @get_return_addr() {
  %ra = call ptr @llvm.returnaddress(i32 0)
  ret ptr %ra
}
