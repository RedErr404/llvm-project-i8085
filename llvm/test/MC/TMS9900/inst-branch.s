; RUN: llvm-mc -triple tms9900 -show-encoding < %s | FileCheck %s

start:
  jeq target
  jne target
  jgt target
  jlt target
  jh target
  jhe target
  jl target
  jle target
  joc target
  jnc target
  jno target
  jop target
  jmp target
  nop
  b @target
  bl @target

target:

; CHECK: JEQ{{[ \t]+}}target{{[ \t]+}}; encoding: [0x13'A',0x00]
; CHECK: fixup A - offset: 0, value: target, kind: fixup_tms9900_pcrel_8
; CHECK: JNE{{[ \t]+}}target{{[ \t]+}}; encoding: [0x16'A',0x00]
; CHECK: fixup A - offset: 0, value: target, kind: fixup_tms9900_pcrel_8
; CHECK: JGT{{[ \t]+}}target{{[ \t]+}}; encoding: [0x15'A',0x00]
; CHECK: fixup A - offset: 0, value: target, kind: fixup_tms9900_pcrel_8
; CHECK: JLT{{[ \t]+}}target{{[ \t]+}}; encoding: [0x11'A',0x00]
; CHECK: fixup A - offset: 0, value: target, kind: fixup_tms9900_pcrel_8
; CHECK: JH{{[ \t]+}}target{{[ \t]+}}; encoding: [0x1b'A',0x00]
; CHECK: fixup A - offset: 0, value: target, kind: fixup_tms9900_pcrel_8
; CHECK: JHE{{[ \t]+}}target{{[ \t]+}}; encoding: [0x14'A',0x00]
; CHECK: fixup A - offset: 0, value: target, kind: fixup_tms9900_pcrel_8
; CHECK: JL{{[ \t]+}}target{{[ \t]+}}; encoding: [0x1a'A',0x00]
; CHECK: fixup A - offset: 0, value: target, kind: fixup_tms9900_pcrel_8
; CHECK: JLE{{[ \t]+}}target{{[ \t]+}}; encoding: [0x12'A',0x00]
; CHECK: fixup A - offset: 0, value: target, kind: fixup_tms9900_pcrel_8
; CHECK: JOC{{[ \t]+}}target{{[ \t]+}}; encoding: [0x18'A',0x00]
; CHECK: fixup A - offset: 0, value: target, kind: fixup_tms9900_pcrel_8
; CHECK: JNC{{[ \t]+}}target{{[ \t]+}}; encoding: [0x17'A',0x00]
; CHECK: fixup A - offset: 0, value: target, kind: fixup_tms9900_pcrel_8
; CHECK: JNO{{[ \t]+}}target{{[ \t]+}}; encoding: [0x19'A',0x00]
; CHECK: fixup A - offset: 0, value: target, kind: fixup_tms9900_pcrel_8
; CHECK: JOP{{[ \t]+}}target{{[ \t]+}}; encoding: [0x1c'A',0x00]
; CHECK: fixup A - offset: 0, value: target, kind: fixup_tms9900_pcrel_8
; CHECK: JMP{{[ \t]+}}target{{[ \t]+}}; encoding: [0x10'A',0x00]
; CHECK: fixup A - offset: 0, value: target, kind: fixup_tms9900_pcrel_8
; CHECK: NOP{{[ \t]+}}; encoding: [0x10,0x00]
; CHECK: B{{[ \t]+}}@target{{[ \t]+}}; encoding: [0x04,0x60,A,A]
; CHECK: fixup A - offset: 2, value: target, kind: fixup_tms9900_16
; CHECK: BL{{[ \t]+}}@target{{[ \t]+}}; encoding: [0x06,0xa0,A,A]
; CHECK: fixup A - offset: 2, value: target, kind: fixup_tms9900_16
