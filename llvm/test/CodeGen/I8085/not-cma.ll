; RUN: llc -mattr=i8085,sram < %s -march=i8085 -verify-machineinstrs | FileCheck %s

; ~x (xor x, -1) should lower to CMA (1 byte, 4T, flag-preserving) rather than
; XRI 0xFF (2 bytes, 7T, clobbers all flags).

define i8 @not8(i8 %x) {
; CHECK-LABEL: not8:
; CHECK:      MOV A, B
; CHECK-NEXT: CMA
; CHECK-NOT:  XRI
  %r = xor i8 %x, -1
  ret i8 %r
}

; NOT feeding further arithmetic still uses CMA for the complement.
define i8 @not8_then_add(i8 %x, i8 %y) {
; CHECK-LABEL: not8_then_add:
; CHECK:      CMA
; CHECK-NOT:  XRI
  %n = xor i8 %x, -1
  %r = add i8 %n, %y
  ret i8 %r
}
