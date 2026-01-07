# RUN: llvm-mc -triple tms9900 -show-encoding %s \
# RUN:     | FileCheck %s --check-prefix=ENC
# RUN: llvm-mc -triple tms9900 -filetype=obj %s \
# RUN:     | llvm-objdump -d - | FileCheck %s --check-prefix=DIS

        b  *r11
        bl *r11

; ENC: {{[Bb]}}{{[[:space:]]+}}*{{[Rr]}}11
; ENC-SAME: encoding: [0x04,0x5b]
; ENC: {{[Bb]}}{{[Ll]}}{{[[:space:]]+}}*{{[Rr]}}11
; ENC-SAME: encoding: [0x06,0x9b]
; DIS: {{[Bb]}}{{[[:space:]]+}}*{{[Rr]}}11
; DIS: {{[Bb]}}{{[Ll]}}{{[[:space:]]+}}*{{[Rr]}}11
