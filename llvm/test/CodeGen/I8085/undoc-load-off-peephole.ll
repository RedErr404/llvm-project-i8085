; RUN: llc -mtriple=i8085-unknown-elf -mattr=+undoc -O2 < %s | FileCheck %s --check-prefix=UNDOC
; RUN: llc -mtriple=i8085-unknown-elf -O2 < %s | FileCheck %s --check-prefix=PLAIN

; End-to-end integration check for the LDHI post-RA peephole. A function reading
; several 16-bit struct fields through a pointer spills the pointer and reloads
; base+offset into HL (LXI D,off ; DAD D) before each byte-wise deref. Under
; +undoc the peephole folds at least one such site into LDHI ; LHLX. Without
; undoc no LDHI is emitted. (The precise, RA-independent behavior lives in
; undoc-load-off-peephole.mir; this only asserts the fold reaches real codegen.)

%struct.S = type { i16, i16, i16, i8, i16, i16, i16 }

define i16 @sum_fields(ptr %p) {
; UNDOC-LABEL: sum_fields:
; UNDOC: LDHI
; UNDOC: LHLX
; PLAIN-LABEL: sum_fields:
; PLAIN-NOT: LDHI
entry:
  %pa = getelementptr inbounds %struct.S, ptr %p, i16 0, i32 0
  %a = load i16, ptr %pa
  %pb = getelementptr inbounds %struct.S, ptr %p, i16 0, i32 1
  %b = load i16, ptr %pb
  %pc = getelementptr inbounds %struct.S, ptr %p, i16 0, i32 2
  %c = load i16, ptr %pc
  %pe = getelementptr inbounds %struct.S, ptr %p, i16 0, i32 4
  %e = load i16, ptr %pe
  %pf = getelementptr inbounds %struct.S, ptr %p, i16 0, i32 5
  %f = load i16, ptr %pf
  %pg = getelementptr inbounds %struct.S, ptr %p, i16 0, i32 6
  %g = load i16, ptr %pg
  %s1 = add i16 %a, %b
  %s2 = add i16 %s1, %c
  %s3 = add i16 %s2, %e
  %s4 = add i16 %s3, %f
  %s5 = add i16 %s4, %g
  ret i16 %s5
}
