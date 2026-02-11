; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test 64-bit (i64) integer operations on the 16-bit TMS9900.
;
; Type legalization path: i64 -> split to two i32 -> each split to two i16.
; So i64 becomes four i16 register values.
;
; Calling convention: i64 passed/returned as 4 x i16 in R0:R1:R2:R3
; (high word first). Second i64 argument spills to stack.
;
; Operations:
; - add/sub: cascaded carry/borrow propagation across 4 words (inline)
; - mul/div/rem: libcalls (__muldi3, __divdi3, __udivdi3, __moddi3, __umoddi3)
; - shifts: libcalls for variable (__ashldi3, __lshrdi3, __ashrdi3), inline for constant
; - bitwise (and/or/xor): word-parallel inline operations
; - comparisons: multi-word compare sequences
; - conversions: zero/sign extend from i32/i16, truncate to i32/i16

target datalayout = "e-m:e-p:16:16-i16:16-a:0:16-n16"

; ==========================================================================
; Arithmetic operations
; ==========================================================================

; --- 64-bit addition (inline, no libcall) ---
; Should expand to cascaded word adds with carry propagation.
; CHECK-LABEL: add64:
; CHECK-NOT: BL @__adddi3
; CHECK: A{{[ \t]}}
; CHECK: B{{[ \t]+}}*R11
define i64 @add64(i64 %a, i64 %b) {
  %r = add i64 %a, %b
  ret i64 %r
}

; --- 64-bit subtraction (inline, no libcall) ---
; Should expand to cascaded word subtracts with borrow propagation.
; CHECK-LABEL: sub64:
; CHECK-NOT: BL @__subdi3
; CHECK: S{{[ \t]}}
; CHECK: B{{[ \t]+}}*R11
define i64 @sub64(i64 %a, i64 %b) {
  %r = sub i64 %a, %b
  ret i64 %r
}

; --- 64-bit multiply (libcall) ---
; CHECK-LABEL: mul64:
; CHECK: BL @__muldi3
; CHECK: B{{[ \t]+}}*R11
define i64 @mul64(i64 %a, i64 %b) {
  %r = mul i64 %a, %b
  ret i64 %r
}

; --- 64-bit unsigned divide (libcall) ---
; CHECK-LABEL: udiv64:
; CHECK: BL @__udivdi3
; CHECK: B{{[ \t]+}}*R11
define i64 @udiv64(i64 %a, i64 %b) {
  %r = udiv i64 %a, %b
  ret i64 %r
}

; --- 64-bit signed divide (libcall) ---
; CHECK-LABEL: sdiv64:
; CHECK: BL @__divdi3
; CHECK: B{{[ \t]+}}*R11
define i64 @sdiv64(i64 %a, i64 %b) {
  %r = sdiv i64 %a, %b
  ret i64 %r
}

; --- 64-bit unsigned remainder (libcall) ---
; CHECK-LABEL: urem64:
; CHECK: BL @__umoddi3
; CHECK: B{{[ \t]+}}*R11
define i64 @urem64(i64 %a, i64 %b) {
  %r = urem i64 %a, %b
  ret i64 %r
}

; --- 64-bit signed remainder (libcall) ---
; CHECK-LABEL: srem64:
; CHECK: BL @__moddi3
; CHECK: B{{[ \t]+}}*R11
define i64 @srem64(i64 %a, i64 %b) {
  %r = srem i64 %a, %b
  ret i64 %r
}

; ==========================================================================
; Shift operations
; ==========================================================================

; --- 64-bit shift left (variable, uses i32 shift libcalls) ---
; The i64 shift is decomposed into i32 shift pairs.
; CHECK-LABEL: shl64:
; CHECK: BL @__ashlsi3
; CHECK: B{{[ \t]+}}*R11
define i64 @shl64(i64 %a, i64 %b) {
  %r = shl i64 %a, %b
  ret i64 %r
}

; --- 64-bit logical shift right (variable, uses i32 shift libcalls) ---
; CHECK-LABEL: lshr64:
; CHECK: BL @__lshrsi3
; CHECK: B{{[ \t]+}}*R11
define i64 @lshr64(i64 %a, i64 %b) {
  %r = lshr i64 %a, %b
  ret i64 %r
}

; --- 64-bit arithmetic shift right (variable, uses i32 shift libcalls) ---
; CHECK-LABEL: ashr64:
; CHECK: BL @__ashrsi3
; CHECK: B{{[ \t]+}}*R11
define i64 @ashr64(i64 %a, i64 %b) {
  %r = ashr i64 %a, %b
  ret i64 %r
}

; --- 64-bit shift left by constant 1 (inline, no libcall) ---
; Constant shift by 1 should use inline SLA/SRL bit manipulation.
; CHECK-LABEL: shl_const1:
; CHECK-NOT: BL
; CHECK: SLA{{[ \t]+}}R3,1
; CHECK: B{{[ \t]+}}*R11
define i64 @shl_const1(i64 %a) {
  %r = shl i64 %a, 1
  ret i64 %r
}

; --- 64-bit shift left by constant 32 (inline word move) ---
; Shifting by 32 should just move the low i32 to the high i32 and zero the low.
; CHECK-LABEL: shl_const32:
; CHECK-NOT: BL
; CHECK: CLR{{[ \t]+}}R{{[23]}}
; CHECK: B{{[ \t]+}}*R11
define i64 @shl_const32(i64 %a) {
  %r = shl i64 %a, 32
  ret i64 %r
}

; ==========================================================================
; Bitwise operations (all inline, word-parallel)
; ==========================================================================

; --- 64-bit AND ---
; TMS9900 AND is done via INV+SZC (invert-then-set-zeros-corresponding).
; CHECK-LABEL: and64:
; CHECK-NOT: BL
; CHECK: INV{{[ \t]+}}R{{[0-9]+}}
; CHECK: SZC{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: B{{[ \t]+}}*R11
define i64 @and64(i64 %a, i64 %b) {
  %r = and i64 %a, %b
  ret i64 %r
}

; --- 64-bit OR ---
; Uses SOC (Set Ones Corresponding) on each word pair.
; CHECK-LABEL: or64:
; CHECK-NOT: BL
; CHECK: SOC{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: B{{[ \t]+}}*R11
define i64 @or64(i64 %a, i64 %b) {
  %r = or i64 %a, %b
  ret i64 %r
}

; --- 64-bit XOR ---
; Uses XOR instruction on each word pair.
; CHECK-LABEL: xor64:
; CHECK-NOT: BL
; CHECK: XOR{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: B{{[ \t]+}}*R11
define i64 @xor64(i64 %a, i64 %b) {
  %r = xor i64 %a, %b
  ret i64 %r
}

; --- 64-bit NOT (XOR with -1) ---
; Should use INV instruction on each word.
; CHECK-LABEL: not64:
; CHECK-NOT: BL
; CHECK: INV{{[ \t]+}}R0
; CHECK: INV{{[ \t]+}}R1
; CHECK: INV{{[ \t]+}}R2
; CHECK: INV{{[ \t]+}}R3
; CHECK: B{{[ \t]+}}*R11
define i64 @not64(i64 %a) {
  %r = xor i64 %a, -1
  ret i64 %r
}

; ==========================================================================
; Comparison operations
; ==========================================================================

; --- 64-bit equality ---
; XOR all word pairs and OR results; zero means equal.
; CHECK-LABEL: eq64:
; CHECK: XOR{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: SOC{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: B{{[ \t]+}}*R11
define i1 @eq64(i64 %a, i64 %b) {
  %r = icmp eq i64 %a, %b
  ret i1 %r
}

; --- 64-bit inequality ---
; Same as equality but with inverted result.
; CHECK-LABEL: ne64:
; CHECK: XOR{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: SOC{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; CHECK: B{{[ \t]+}}*R11
define i1 @ne64(i64 %a, i64 %b) {
  %r = icmp ne i64 %a, %b
  ret i1 %r
}

; --- 64-bit unsigned less than ---
; Multi-word unsigned comparison.
; CHECK-LABEL: ult64:
; CHECK-NOT: BL
; CHECK: B{{[ \t]+}}*R11
define i1 @ult64(i64 %a, i64 %b) {
  %r = icmp ult i64 %a, %b
  ret i1 %r
}

; --- 64-bit signed less than ---
; Multi-word signed comparison (top word signed, lower words unsigned).
; CHECK-LABEL: slt64:
; CHECK-NOT: BL
; CHECK: B{{[ \t]+}}*R11
define i1 @slt64(i64 %a, i64 %b) {
  %r = icmp slt i64 %a, %b
  ret i1 %r
}

; ==========================================================================
; Type conversion operations
; ==========================================================================

; --- Zero extend i32 to i64 ---
; Upper 32 bits (R0:R1) should be cleared, lower 32 bits (R2:R3) get the value.
; CHECK-LABEL: zext32to64:
; CHECK-DAG: CLR{{[ \t]+}}R0
; CHECK-DAG: CLR{{[ \t]+}}R1
; CHECK: B{{[ \t]+}}*R11
define i64 @zext32to64(i32 %x) {
  %r = zext i32 %x to i64
  ret i64 %r
}

; --- Sign extend i32 to i64 ---
; Upper 32 bits get sign-extended from the top bit of the i32 value.
; SRA R0,15 creates the sign extension word (all 0s or all 1s).
; CHECK-LABEL: sext32to64:
; CHECK: SRA{{[ \t]+}}R0,15
; CHECK: B{{[ \t]+}}*R11
define i64 @sext32to64(i32 %x) {
  %r = sext i32 %x to i64
  ret i64 %r
}

; --- Zero extend i16 to i64 ---
; Only lowest word (R3) has value, upper three words cleared.
; CHECK-LABEL: zext16to64:
; CHECK: CLR{{[ \t]+}}R{{[012]}}
; CHECK: B{{[ \t]+}}*R11
define i64 @zext16to64(i16 %x) {
  %r = zext i16 %x to i64
  ret i64 %r
}

; --- Truncate i64 to i32 ---
; Just keep the low two registers (R2:R3 -> R0:R1).
; CHECK-LABEL: trunc64to32:
; CHECK: MOV{{[ \t]+}}R2,R0
; CHECK-NEXT: B{{[ \t]+}}*R11
define i32 @trunc64to32(i64 %x) {
  %r = trunc i64 %x to i32
  ret i32 %r
}

; --- Truncate i64 to i16 ---
; Just keep the lowest register (R3 -> R0).
; CHECK-LABEL: trunc64to16:
; CHECK: MOV{{[ \t]+}}R3,R0
; CHECK-NEXT: B{{[ \t]+}}*R11
define i16 @trunc64to16(i64 %x) {
  %r = trunc i64 %x to i16
  ret i16 %r
}

; ==========================================================================
; Load and store
; ==========================================================================

; --- 64-bit load from pointer ---
; Should generate 4 consecutive word loads.
; CHECK-LABEL: load64:
; CHECK: MOV{{[ \t]+}}*R{{[0-9]+}},R0
; CHECK: MOV{{[ \t]+}}@2(R{{[0-9]+}}),R1
; CHECK: MOV{{[ \t]+}}@4(R{{[0-9]+}}),R2
; CHECK: MOV{{[ \t]+}}@6(R{{[0-9]+}}),R3
; CHECK: B{{[ \t]+}}*R11
define i64 @load64(ptr %p) {
  %v = load i64, ptr %p
  ret i64 %v
}

; --- 64-bit store to pointer ---
; Should generate 4 consecutive word stores.
; CHECK-LABEL: store64:
; CHECK: MOV{{[ \t]+}}R{{[0-9]+}},@6(R{{[0-9]+}})
; CHECK: MOV{{[ \t]+}}R{{[0-9]+}},@4(R{{[0-9]+}})
; CHECK: MOV{{[ \t]+}}R{{[0-9]+}},@2(R{{[0-9]+}})
; CHECK: MOV{{[ \t]+}}R{{[0-9]+}},*R{{[0-9]+}}
; CHECK: B{{[ \t]+}}*R11
define void @store64(i64 %v, ptr %p) {
  store i64 %v, ptr %p
  ret void
}

; ==========================================================================
; Constant materialization
; ==========================================================================

; --- 64-bit constant return ---
; 1234567890123 = 0x000_011F_71FB_04CB
; R0=0, R1=0x011F, R2=0x71FB, R3=0x04CB
; CHECK-LABEL: const64:
; CHECK: CLR{{[ \t]+}}R0
; CHECK: LI{{[ \t]+}}R1,287
; CHECK: LI{{[ \t]+}}R2,29179
; CHECK: LI{{[ \t]+}}R3,1227
; CHECK: B{{[ \t]+}}*R11
define i64 @const64() {
  ret i64 1234567890123
}
