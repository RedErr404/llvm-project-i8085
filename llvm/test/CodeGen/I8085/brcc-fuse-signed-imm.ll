; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s

; A signed i8 compare against a constant feeding a branch fuses to the
; biased-immediate sequence  MOV A,lhs ; XRI 80h ; CPI (k^80h) ; Jcc
; instead of the same-sign/different-sign boolean diamond. So we expect XRI 128
; and a CPI, and NOT the boolean re-test "ORA A".

declare void @sink()

; if ((int8)a < 0) -- the ubiquitous sign test
define void @slt0(i8 %a) {
; CHECK-LABEL: slt0:
; CHECK:       XRI 128
; CHECK:       CPI
; CHECK-NOT:   ORA A
entry:
  %c = icmp slt i8 %a, 0
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}

; if ((int8)a >= 0)
define void @sge0(i8 %a) {
; CHECK-LABEL: sge0:
; CHECK:       XRI 128
; CHECK:       CPI
; CHECK-NOT:   ORA A
entry:
  %c = icmp sge i8 %a, 0
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}

; if ((int8)a < 5)
define void @slt5(i8 %a) {
; CHECK-LABEL: slt5:
; CHECK:       XRI 128
; CHECK:       CPI
; CHECK-NOT:   ORA A
entry:
  %c = icmp slt i8 %a, 5
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}

; if ((int8)a > 5)  -- uses the a > k  ==  a >= k+1 rewrite
define void @sgt5(i8 %a) {
; CHECK-LABEL: sgt5:
; CHECK:       XRI 128
; CHECK:       CPI
; CHECK-NOT:   ORA A
entry:
  %c = icmp sgt i8 %a, 5
  br i1 %c, label %t, label %f
t:
  call void @sink()
  br label %f
f:
  ret void
}
