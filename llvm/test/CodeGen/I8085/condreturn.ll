; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s

; A conditional branch to a basic block whose only instruction is a bare RET
; should become a conditional return (Rcc). Rcc affects no registers and no
; flags, so this is a pure control-flow substitution. It must NOT fire when the
; branch target does stack cleanup (epilogue) before returning.

; Frameless guard clause: `if (n == 0) return;` -> the JZ to the bare-RET block
; becomes RZ.
define void @guard(ptr %p, i8 %n) {
; CHECK-LABEL: guard:
; CHECK:       RZ
; CHECK-NOT:   JZ
entry:
  %c = icmp eq i8 %n, 0
  br i1 %c, label %ret, label %store
store:
  store i8 %n, ptr %p
  br label %ret
ret:
  ret void
}

; (Value-returning early returns generally don't apply: the target block first
; materializes the value, e.g. `MVI A,7; RET`, so it is not a bare-RET block and
; must be left as a normal branch. Which condition code appears also depends on
; block-layout heuristics, so exhaustive Rcc opcode coverage and the negative
; case live in peephole-condreturn.mir.)
