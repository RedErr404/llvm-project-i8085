; RUN: llc -mtriple=tms9900 -stop-after=finalize-isel < %s | FileCheck %s
; RUN: llc -mtriple=tms9900 -O2 < %s | FileCheck %s --check-prefix=ASM
;
; Verify SELECT16 emits CMPBR in MIR and C/Jcc stay adjacent in asm.

define i16 @select16_cmpbr(i16 %a, i16 %b, i16 %t, i16 %f) {
entry:
  %cmp = icmp slt i16 %a, %b
  %sel = select i1 %cmp, i16 %t, i16 %f
  ret i16 %sel
}

; CHECK-LABEL: name: select16_cmpbr
; CHECK: CMPBRrr
; CHECK: PHI

; ASM-LABEL: select16_cmpbr
; ASM: C{{[ \t]+}}R{{[0-9]+}},R{{[0-9]+}}
; ASM-NEXT: {{J[EGHLNOC][A-Z]*}}{{[ \t]+}}[[TARGET:LBB[0-9_]+]]
; ASM-NEXT: {{J[EGHLNOC][A-Z]*}}{{[ \t]+}}[[TARGET]]
