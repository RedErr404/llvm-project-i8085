; RUN: llc -march=tms9900 -O0 < %s | FileCheck %s
;
; Test all five TMS9900 addressing modes:
;   1. Register direct:    MOV R0, R1
;   2. Register indirect:  MOV *R0, R1
;   3. Indexed:            MOV @offset(R1), R0
;   4. Symbolic/absolute:  MOV @addr, R0
;   5. Auto-increment:     MOV *R0+, R1  (tested via post-increment peephole)
;
; This test uses -O0 to get predictable register allocation.

@gval = external global i16

; --- 1. Register direct ---
; Two register operands, no memory reference.
; CHECK-LABEL: reg_direct:
; CHECK: A{{[ \t]+}}R1,R0
; CHECK: B{{[ \t]+}}*R11

define i16 @reg_direct(i16 %a, i16 %b) {
  %r = add i16 %a, %b
  ret i16 %r
}

; --- 2. Register indirect (load) ---
; Load from pointer: MOV *Rx,Ry
; CHECK-LABEL: reg_indirect_load:
; CHECK: MOV{{[ \t]+}}*R0,R0
; CHECK: B{{[ \t]+}}*R11

define i16 @reg_indirect_load(ptr %p) {
  %v = load i16, ptr %p
  ret i16 %v
}

; --- 2b. Register indirect (store) ---
; Store through pointer: MOV Ry,*Rx
; CHECK-LABEL: reg_indirect_store:
; CHECK: MOV{{[ \t]+}}R1,*R0
; CHECK: B{{[ \t]+}}*R11

define void @reg_indirect_store(ptr %p, i16 %val) {
  store i16 %val, ptr %p
  ret void
}

; --- 3. Indexed (constant offset from register) ---
; Access at a constant byte offset from a register.
; CHECK-LABEL: indexed_load:
; CHECK: MOV{{[ \t]+}}@6(R{{[0-9]+}}),R0
; CHECK: B{{[ \t]+}}*R11

define i16 @indexed_load(ptr %p) {
  %p2 = getelementptr i16, ptr %p, i16 3
  %v = load i16, ptr %p2
  ret i16 %v
}

; --- 4. Symbolic/absolute (global variable) ---
; Load from absolute address: MOV @gval,R0
; CHECK-LABEL: symbolic_load:
; CHECK: MOV{{[ \t]+}}@gval,R0
; CHECK: B{{[ \t]+}}*R11

define i16 @symbolic_load() {
  %v = load i16, ptr @gval
  ret i16 %v
}

; --- 4b. Symbolic/absolute store ---
; Store to absolute address: MOV R0,@gval
; CHECK-LABEL: symbolic_store:
; CHECK: MOV{{[ \t]+}}R0,@gval
; CHECK: B{{[ \t]+}}*R11

define void @symbolic_store(i16 %val) {
  store i16 %val, ptr @gval
  ret void
}

; --- 4c. Address-of global ---
; Load address of global into register: LI R0,gval
; CHECK-LABEL: addr_of_global:
; CHECK: LI{{[ \t]+}}R0,gval
; CHECK: B{{[ \t]+}}*R11

define ptr @addr_of_global() {
  ret ptr @gval
}
