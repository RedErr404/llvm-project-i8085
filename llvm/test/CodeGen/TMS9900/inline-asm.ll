; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s
;
; Test inline assembly support.
; The TMS9900 backend should pass through inline asm and handle
; register constraints.

; --- Simple inline asm with no operands ---
; CHECK-LABEL: asm_nop:
; CHECK: ;APP
; CHECK: NOP
; CHECK: ;NO_APP
; CHECK: B{{[ \t]+}}*R11

define void @asm_nop() {
  call void asm sideeffect "NOP", ""()
  ret void
}

; --- Inline asm with input register constraint ---
; CHECK-LABEL: asm_with_input:
; CHECK: ;APP
; CHECK: ;NO_APP
; CHECK: B{{[ \t]+}}*R11

define void @asm_with_input(i16 %val) {
  call void asm sideeffect "", "r"(i16 %val)
  ret void
}
