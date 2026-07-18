; RUN: llc -mattr=i8085,sram < %s -march=i8085 | FileCheck %s

; Function pointer / indirect call patterns.
; Adapted from RISC-V calls.ll indirect call tests.
; i8085 uses PCHL for indirect calls (push return addr, then PCHL).

declare i16 @external_func(i16)

; Direct call to external function
define i16 @test_direct_call(i16 %a) {
; CHECK-LABEL: test_direct_call:
; CHECK: CALL external_func
; CHECK: RET
entry:
  %r = call i16 @external_func(i16 %a)
  ret i16 %r
}

; Indirect call via function pointer (uses PCHL)
define i16 @test_indirect_call(i16 (i16)* %fptr, i16 %arg) {
; CHECK-LABEL: test_indirect_call:
; CHECK: PUSH B
; CHECK: PCHL
; CHECK: RET
entry:
  %r = call i16 %fptr(i16 %arg)
  ret i16 %r
}

; Call that returns i8 via indirect pointer
define i8 @test_call_ret_i8(i8 (i8)* %fptr, i8 %arg) {
; CHECK-LABEL: test_call_ret_i8:
; CHECK: PUSH B
; CHECK: PCHL
; CHECK: RET
entry:
  %r = call i8 %fptr(i8 %arg)
  ret i8 %r
}

; Call function pointer stored in a global
@gfptr = global i16 (i16)* null

define i16 @test_global_funcptr(i16 %arg) {
; The function pointer (16-bit global) is loaded with the direct LHLD.
; CHECK-LABEL: test_global_funcptr:
; CHECK: LHLD gfptr
; CHECK: PUSH B
; CHECK: PCHL
; CHECK: RET
entry:
  %fptr = load i16 (i16)*, i16 (i16)** @gfptr
  %r = call i16 %fptr(i16 %arg)
  ret i16 %r
}
