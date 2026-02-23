; RUN: llc -march=tms9900 -O0 < %s | FileCheck %s

define zeroext i8 @low(i16 %x) #0 {
entry:
  %slot = alloca i16, align 2
  store i16 %x, ptr %slot, align 2
  %val = load i16, ptr %slot, align 2
  %and = and i16 %val, 255
  %trunc = trunc i16 %and to i8
  ret i8 %trunc
}

; CHECK-LABEL: low:
; CHECK: MOV R10,[[REG:R[0-9]+]]
; CHECK: MOV R0,*[[REG]]
; CHECK: MOVB @1([[REG]]),R0

attributes #0 = { noinline nounwind optnone }
