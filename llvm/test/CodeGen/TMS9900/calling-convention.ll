; RUN: llc -march=tms9900 -O0 < %s | FileCheck %s
;
; Test the TMS9900 calling convention:
;   - First 4 arguments in R0-R3
;   - 5th+ arguments on stack
;   - Return value in R0 (i16) or R0:R1 (i32)
;   - R11 (link register) saved/restored for non-leaf functions
;   - Leaf functions do NOT save R11

declare i16 @external(i16)
declare i16 @takes5(i16, i16, i16, i16, i16)

; --- Leaf function: no R11 save needed ---
; A function that doesn't call anything should not save/restore R11.
; CHECK-LABEL: leaf_func:
; CHECK-NOT: MOV{{[ \t]+}}R11
; CHECK: INC{{[ \t]+}}R0
; CHECK: B{{[ \t]+}}*R11

define i16 @leaf_func(i16 %a) {
entry:
  %r = add i16 %a, 1
  ret i16 %r
}

; --- Non-leaf function: R11 saved/restored ---
; A function that calls another function must save R11 (link register).
; When StackSize=0, no AI is needed (just DECT+MOV for R11).
; CHECK-LABEL: non_leaf:
; CHECK: DECT{{[ \t]+}}R10
; CHECK: MOV{{[ \t]+}}R11,*R10
; CHECK: BL{{[ \t]+}}@external
; CHECK: MOV{{[ \t]+}}*R10+,R11
; CHECK: B{{[ \t]+}}*R11

define i16 @non_leaf(i16 %a) {
entry:
  %r = call i16 @external(i16 %a)
  ret i16 %r
}

; --- 5th argument goes on stack ---
; The 5th argument should be pushed onto the stack before the BL.
; StackSize=4 (outgoing arg) + 2 (alignment padding after DECT) = AI -6.
; CHECK-LABEL: pass_5_args:
; CHECK: DECT{{[ \t]+}}R10
; CHECK: MOV{{[ \t]+}}R11,*R10
; CHECK: AI{{[ \t]+}}R10,-6
; CHECK-DAG: LI{{[ \t]+}}R0,1
; CHECK-DAG: LI{{[ \t]+}}R1,2
; CHECK-DAG: LI{{[ \t]+}}R2,3
; CHECK-DAG: LI{{[ \t]+}}R3,4
; CHECK: BL{{[ \t]+}}@takes5

define i16 @pass_5_args() {
entry:
  %r = call i16 @takes5(i16 1, i16 2, i16 3, i16 4, i16 5)
  ret i16 %r
}

; --- i32 return: R0 (high) and R1 (low) ---
; A 32-bit value is returned in R0:R1.
; CHECK-LABEL: ret_i32:
; CHECK: B{{[ \t]+}}*R11

define i32 @ret_i32(i32 %a) {
  ret i32 %a
}

; --- i16 return in R0 ---
; CHECK-LABEL: ret_i16:
; CHECK: LI{{[ \t]+}}R0,42
; CHECK: B{{[ \t]+}}*R11

define i16 @ret_i16() {
  ret i16 42
}

; --- i32 argument split into R0:R1 ---
; A 32-bit argument occupies R0 (high) and R1 (low).
; CHECK-LABEL: arg_i32:
; CHECK: A{{[ \t]+}}R1,R0
; CHECK: B{{[ \t]+}}*R11

define i16 @arg_i32(i32 %a) {
  %lo = trunc i32 %a to i16
  %hi32 = lshr i32 %a, 16
  %hi = trunc i32 %hi32 to i16
  %sum = add i16 %lo, %hi
  ret i16 %sum
}
